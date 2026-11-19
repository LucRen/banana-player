#include "viztap.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
#ifdef __linux__
#include <sys/resource.h>
#endif

namespace bp
{
	namespace
	{
		uint64_t nowNs()
		{
			return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		// the song's sound, decoded and resampled to mono VN_RATE on demand around a time (the worker's alone)
		struct Decoder
		{
			AVFormatContext* fmt = nullptr;
			AVCodecContext* dec = nullptr;
			SwrContext* swr = nullptr;
			AVPacket* pkt = nullptr;
			AVFrame* frame = nullptr;
			int stream = -1;
			double tb = 0, start = 0;	// the stream's time base; the file's start (mpv counts from it: rebase-start-time)
			bool eof = false;
			// the samples kept: buf[0] is at t0 seconds (t0 < 0: none since the last seek)
			std::vector<float> buf;
			double t0 = -1;
			// the input the resampler was made for
			int swrRate = 0, swrFmt = -1;
			AVChannelLayout swrLayout{};
			std::string info;

			~Decoder()
			{
				swr_free(&swr);
				av_channel_layout_uninit(&swrLayout);
				av_frame_free(&frame);
				av_packet_free(&pkt);
				avcodec_free_context(&dec);
				if (fmt) avformat_close_input(&fmt);
			}

			bool open(const std::string& path)
			{
				if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return false;
				if (avformat_find_stream_info(fmt, nullptr) < 0) return false;
				// the first audio stream: the one the blank graph's [aid1] plays
				for (unsigned i = 0; i < fmt->nb_streams; i++)
				{
					if (stream < 0 && fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) stream = (int)i;
					else fmt->streams[i]->discard = AVDISCARD_ALL;	// the art, other tracks: not even demuxed
				}
				if (stream < 0) return false;
				AVStream* st = fmt->streams[stream];
				const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
				if (!codec) return false;
				dec = avcodec_alloc_context3(codec);
				if (!dec || avcodec_parameters_to_context(dec, st->codecpar) < 0) return false;
				dec->thread_count = 1;
				if (avcodec_open2(dec, codec, nullptr) < 0) return false;
				pkt = av_packet_alloc();
				frame = av_frame_alloc();
				if (!pkt || !frame) return false;
				tb = av_q2d(st->time_base);
				start = fmt->start_time != AV_NOPTS_VALUE ? fmt->start_time / (double)AV_TIME_BASE : 0;
				char s[160];
				snprintf(s, sizeof s, "%s %d Hz %d ch", codec->name, dec->sample_rate, dec->ch_layout.nb_channels);
				info = s;
				return true;
			}

			double end() const { return t0 < 0 ? -1 : t0 + (double)buf.size() / VN_RATE; }

			void seek(double t)
			{
				int64_t ts = (int64_t)((std::max(0.0, t) + start) / tb);
				av_seek_frame(fmt, stream, ts, AVSEEK_FLAG_BACKWARD);
				avcodec_flush_buffers(dec);
				swr_free(&swr);	// (its buffered tail is the old place's)
				buf.clear();
				t0 = -1;
				eof = false;
			}

			bool resampler(const AVFrame* f)
			{
				if (swr && swrRate == f->sample_rate && swrFmt == f->format && !av_channel_layout_compare(&swrLayout, &f->ch_layout)) return true;
				swr_free(&swr);
				av_channel_layout_uninit(&swrLayout);
				AVChannelLayout in{};
				if (f->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default(&in, f->ch_layout.nb_channels);
				else av_channel_layout_copy(&in, &f->ch_layout);
				AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
				int r = swr_alloc_set_opts2(&swr, &mono, AV_SAMPLE_FMT_FLT, VN_RATE, &in, (AVSampleFormat)f->format, f->sample_rate, 0, nullptr);
				av_channel_layout_uninit(&in);
				if (r < 0 || swr_init(swr) < 0)
				{
					swr_free(&swr);
					return false;
				}
				swrRate = f->sample_rate;
				swrFmt = f->format;
				av_channel_layout_copy(&swrLayout, &f->ch_layout);
				return true;
			}

			void append(AVFrame* f)
			{
				if (!resampler(f)) return;
				if (t0 < 0)
				{
					int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
					t0 = pts != AV_NOPTS_VALUE ? pts * tb - start : 0;
				}
				int room = swr_get_out_samples(swr, f->nb_samples);
				if (room <= 0) return;
				size_t at = buf.size();
				buf.resize(at + room);
				uint8_t* out = (uint8_t*)(buf.data() + at);
				int got = swr_convert(swr, &out, room, (const uint8_t**)f->extended_data, f->nb_samples);
				buf.resize(at + std::max(0, got));
			}

			// one packet's worth; false at the end of the file or an error
			bool step()
			{
				if (eof) return false;
				int r = av_read_frame(fmt, pkt);
				if (r < 0)
				{
					avcodec_send_packet(dec, nullptr);	// the decoder's tail
					while (avcodec_receive_frame(dec, frame) == 0) append(frame);
					eof = true;
					return false;
				}
				if (pkt->stream_index == stream && avcodec_send_packet(dec, pkt) >= 0)
					while (avcodec_receive_frame(dec, frame) == 0) append(frame);
				av_packet_unref(pkt);
				return true;
			}

			// the window of n samples centred on t into out (zeros where there's no sound: before, past the end)
			void window(double t, float* out, int n, uint64_t& seeks)
			{
				const double half = n / 2.0 / VN_RATE;
				// a jump (mpv seeked, or the first look): decode from a little before
				if (t0 < 0 || t - half < t0 - 0.05 || t > end() + 0.5)
				{
					if (t0 >= 0) seeks++;
					seek(t - half - 0.25);
				}
				// on to the window's end; a file that won't give its sound stops trying after a while
				for (int i = 0; i < 400 && (t0 < 0 || end() < t + half); i++)
					if (!step()) break;
				const long first = std::lround((t - half - t0) * VN_RATE);
				for (int i = 0; i < n; i++)
				{
					long k = first + i;
					out[i] = t0 >= 0 && k >= 0 && k < (long)buf.size() ? buf[k] : 0.0f;
				}
				// what's behind the window let go (half a second kept, for a small step back)
				if (t0 >= 0 && first > VN_RATE)
				{
					long drop = first - VN_RATE / 2;
					buf.erase(buf.begin(), buf.begin() + drop);
					t0 += (double)drop / VN_RATE;
				}
			}
		};
	}

	struct VizTap::Job
	{
		std::atomic<bool> stop{false}, woken{false};
		std::atomic<int> fps{30}, vis{0};
		std::function<void()> wake;
		std::mutex m;
		std::condition_variable cv;
		VizClock clock;	// (m)
		float targets[VN_MAX_BARS] = {};	// (m)
		int targetsVis = 0, targetsN = 0;	// (m) the style they were measured for
		VizHint hint;	// (m) the loudness and the beats so far
		uint64_t seq = 0;	// (m)
		bool failed = false;	// (m)
		std::string info;	// (m)
		Stats stats;	// (m)
	};

	static void runTap(std::shared_ptr<VizTap::Job> j, std::string path)
	{
#ifdef __linux__
		setpriority(PRIO_PROCESS, 0, 5);	// (this thread alone, on Linux): the song's own decoding first
#endif
		auto d = std::make_unique<Decoder>();
		VizSpectrum spectrum;
		bool ok = d->open(path) && spectrum.ok();
		{
			std::lock_guard<std::mutex> g(j->m);
			j->failed = !ok;
			j->info = ok ? d->info : "couldn't be opened";
		}
		if (!ok)
		{
			fprintf(stderr, "viz-native: %s: the second decode couldn't open it\n", path.c_str());
			if (j->wake) j->wake();
			return;
		}
		int vis = -1;
		VizBands bands;
		std::vector<int> edges;
		std::vector<float> win(VN_FFT), mag(VN_FFT / 2);
		float targets[VN_MAX_BARS];
		uint64_t seeks = 0, spectra = 0;
		VizBeat beat;
		uint32_t beats = 0;
		float loudAvg = 0;
		auto beatAt = std::chrono::steady_clock::now();
		auto loudAt = std::chrono::steady_clock::now();
		auto next = std::chrono::steady_clock::now();
		while (!j->stop)
		{
			VizClock c;
			{
				std::unique_lock<std::mutex> g(j->m);
				// nothing moves (paused, seeking, before mpv's first audio-pts): sleep till told
				j->cv.wait_for(g, std::chrono::milliseconds(250), [&] { return j->stop || (j->clock.moving && j->clock.pts >= 0); });
				c = j->clock;
			}
			if (j->stop) break;
			if (!c.moving || c.pts < 0)
			{
				next = std::chrono::steady_clock::now();
				continue;
			}
			const int fps = std::clamp(j->fps.load(), 30, 60);
			const auto w0 = std::chrono::steady_clock::now();
			// where the sound will be when this frame is on the glass: a frame on (the main thread's draw, the swap)
			const double t = c.at(nowNs()) + 1.0 / fps;
			d->window(t, win.data(), VN_FFT, seeks);
			spectrum.magnitudes(win.data(), mag.data());
			if (j->vis != vis)
			{
				if (vis < 0) beat = VizBeat{};
				// another native style: its own band count (the same 40 Hz..14 kHz, log spaced)
				vis = j->vis;
				bands = vizStyleBands(vis);
				edges = vizBandEdges(bands.bars, VN_FFT, VN_RATE, 40, bands.hi);
			}
			vizBandTargets(mag.data(), edges, VN_RATE, VN_FFT, bands.plain, targets, bands.cut);
			spectra++;
			// the loudness and the beat (Starfield, Helix, Sparks): from the same window, by the real time between looks
			const float loudness = vizLoudness(win.data(), VN_FFT);
			{
				auto t1 = std::chrono::steady_clock::now();
				const double gap = std::chrono::duration<double>(t1 - beatAt).count();
				if (beat.step(vizBassEnergy(mag.data(), VN_RATE, VN_FFT), gap)) beats++;
				// the loudness over about a second (Sleepy moon)
				loudAvg += (loudness - loudAvg) * (float)(1 - std::exp(-std::min(gap, 1.0) / 1.0));
				beatAt = t1;
			}
			bool loud = false;
			for (int i = 0; i < bands.bars; i++) loud = loud || targets[i] > 0;
			auto now = std::chrono::steady_clock::now();
			if (loud) loudAt = now;
			{
				std::lock_guard<std::mutex> g(j->m);
				std::copy(targets, targets + bands.bars, j->targets);
				j->targetsVis = vis;
				j->targetsN = bands.bars;
				j->hint = {loudness, beats, loudAvg};
				j->seq++;
				j->stats.spectra = spectra;
				j->stats.seeks = seeks;
				const double ms = std::chrono::duration<double, std::milli>(now - w0).count();
				j->stats.workMs += ms;
				j->stats.workMax = std::max(j->stats.workMax, ms);
			}
			// silent for half a second (the bars have fallen): no more wakes till there's sound again
			if (now - loudAt < std::chrono::milliseconds(500) && j->wake && !j->woken.exchange(true)) j->wake();
			next += std::chrono::microseconds(1000000 / fps);
			if (next < now)
			{
				next = now;	// (behind: no burst to catch up)
				std::lock_guard<std::mutex> g(j->m);
				j->stats.behind++;
			}
			std::unique_lock<std::mutex> g(j->m);
			j->cv.wait_until(g, next, [&] { return j->stop.load(); });
		}
	}

	void VizTap::start(const std::string& path, int fps, int vis, std::function<void()> wake)
	{
		stop();
		job = std::make_shared<Job>();
		job->fps = fps;
		job->vis = vis;
		job->wake = std::move(wake);
		jobPath = path;
		std::thread(runTap, job, path).detach();
	}

	void VizTap::stop()
	{
		if (!job) return;
		job->stop = true;
		job->cv.notify_all();
		job.reset();	// (the thread holds its own reference till it ends)
		jobPath.clear();
	}

	void VizTap::setFps(int fps)
	{
		if (job) job->fps = fps;
	}

	void VizTap::setStyle(int vis)
	{
		if (job) job->vis = vis;
	}

	void VizTap::setClock(const VizClock& c)
	{
		if (!job) return;
		bool wake;
		{
			std::lock_guard<std::mutex> g(job->m);
			const VizClock& o = job->clock;
			if (o.pts == c.pts && o.atNs == c.atNs && o.speed == c.speed && o.moving == c.moving) return;
			wake = c.moving != o.moving;
			job->clock = c;
			job->stats.clockTold++;
		}
		if (wake) job->cv.notify_all();
	}

	bool VizTap::take(float* out, uint64_t& seq, int& vis, int& n, VizHint& hint)
	{
		if (!job) return false;
		job->woken = false;
		std::lock_guard<std::mutex> g(job->m);
		if (job->seq == seq) return false;
		seq = job->seq;
		std::copy(job->targets, job->targets + job->targetsN, out);
		vis = job->targetsVis;
		n = job->targetsN;
		hint = job->hint;
		return true;
	}

	bool VizTap::failed() const
	{
		if (!job) return false;
		std::lock_guard<std::mutex> g(job->m);
		return job->failed;
	}

	std::string VizTap::info() const
	{
		if (!job) return "";
		std::lock_guard<std::mutex> g(job->m);
		return job->info;
	}

	VizTap::Stats VizTap::stats() const
	{
		if (!job) return {};
		std::lock_guard<std::mutex> g(job->m);
		return job->stats;
	}
}
