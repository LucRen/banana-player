// The README dashboard pictures (docs/img/web-*.png): the real player on the demo library of docs/make-screenshots.sh
// (a player per shot, so each has its theme), driven by headless Chrome (muted) over its DevTools port.
//   node tools/web-screenshots.mjs DEMO_DIR            (build/banana-player must exist)
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const demo = path.resolve(process.argv[2]);
const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const sleep = ms => new Promise(r => setTimeout(r, ms));
const song = path.join(demo, 'Music/Night Drive/01 Neon Highway.mp3');
const song2 = path.join(demo, 'Music/Night Drive/02 Tail Lights.mp3');
const song3 = path.join(demo, 'Music/Paper Boats/01 Paper Boats.mp3');
const movie = path.join(demo, 'Movies/Big Buck Bunny (trailer).mkv');
const PHONE = { w: 390, h: 844, dpr: 3, mobile: true }, DESK = { w: 1440, h: 900, dpr: 2, mobile: false };

// name, theme, size, file played, hash, steps run in the page first (JS), resume (movie)
const shots = (process.env.ONLY ? s => s.filter(x => x[0] === process.env.ONLY) : s => s)([
	['desktop-playing', 'dark-purple', DESK, song, '#/playing', `await clickText('Sound')`],
	['desktop-files', 'light', DESK, song, '#/files/0/Music/Night%20Drive'],
	['desktop-browser', 'midnight-blue', DESK, movie, '#/playing', `await clickText('Chapters')`, 13.2],
	['phone-files', 'sakura', PHONE, song, '#/files/0/Music/Night%20Drive'],
	['phone-playing', 'midnight-blue', PHONE, song, '#/playing'],
	['phone-playlists', 'light', PHONE, song, '#/playlists', `await makeLists()`],
	['phone-video', 'coffee', PHONE, movie, '#/playing', `await clickText('Chapters')`, 13.2],
]);

async function run([name, theme, size, file, hash, steps, at], n) {
	const state = fs.mkdtempSync(path.join(os.tmpdir(), 'bp-webshot-'));
	fs.writeFileSync(path.join(state, 'settings.conf'), `lid-close = pause\ntheme = ${theme}\nmusic-show = both\nvisualizer = ribbon\neq = on\neq-preset = Rock\neq-gains = 4,3,2,0,-1,-1,1,2,3,4\nsound-effect = room\n`);
	if (at) fs.writeFileSync(path.join(state, 'resume.tsv'), `${at}\t1\t1\t${file}\n`);
	const wport = 8100 + n, cport = 9500 + n;
	const player = spawn(path.join(root, 'build/banana-player'), ['--state-dir', state, file], { stdio: 'ignore',
		env: { ...process.env, BP_WEB_FORCE: '1', BP_WEB_PIN: 'ACDE', BP_WEB_PORT: String(wport), BP_BROWSE: demo } });
	const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'bp-webchrome-'));
	const chrome = spawn(CHROME, ['--headless=new', '--mute-audio', '--autoplay-policy=no-user-gesture-required', `--remote-debugging-port=${cport}`,
		`--user-data-dir=${profile}`, '--hide-scrollbars', '--force-color-profile=srgb', '--no-first-run', 'about:blank'], { stdio: 'ignore' });
	let targets;
	for (let i = 0; i < 60; i++) { try { targets = await (await fetch(`http://127.0.0.1:${cport}/json`)).json(); if (targets.find(t => t.type === 'page')) break; } catch {} await sleep(200); }
	const ws = new WebSocket(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
	await new Promise(r => ws.onopen = r);
	let id = 0; const pending = new Map();
	ws.onmessage = m => { const d = JSON.parse(m.data); if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); } };
	const send = (method, params = {}) => new Promise(r => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
	const ev = async e => { const r = (await send('Runtime.evaluate', { expression: e, awaitPromise: true, returnByValue: true })).result; return r.exceptionDetails ? 'ERR ' + r.exceptionDetails.text + JSON.stringify(r.exceptionDetails.exception?.description) : r.result.value; };
	const base = `http://127.0.0.1:${wport}/`;
	await send('Page.enable');
	await send('Emulation.setDeviceMetricsOverride', { width: size.w, height: size.h, deviceScaleFactor: size.dpr, mobile: size.mobile });
	for (let i = 0; i < 50; i++) { try { await fetch(base); break; } catch { await sleep(200); } }
	await send('Page.navigate', { url: base }); await sleep(1500);
	console.log(name, 'login', await ev(`fetch('/api/login',{method:'POST',headers:{'X-BP':'1','Content-Type':'application/x-www-form-urlencoded'},body:'pin=ACDE'}).then(async r=>r.status+' '+await r.text())`));
	await send('Page.navigate', { url: base + '?shot=' + Date.now() + hash }); await sleep(3500);
	const helpers = `const LOG = []; const wait = ms => new Promise(r => setTimeout(r, ms));
		const clickText = async t => { const b = [...document.querySelectorAll('button,[role=tab],a')].find(x => x.textContent.trim().includes(t)); if (b) b.click(); await wait(1200); return !!b; };
		const post = (u, body) => fetch(u, { method: 'POST', headers: { 'X-BP': '1', 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams(body).toString() }).then(r => r.json());
		const makeLists = async () => {
			const files = ${JSON.stringify([song, song2, song3].map(f => '0/' + path.relative(demo, f)))};
			for (const nm of ['Focus', 'Late Night', 'Road Trip', 'Weekend']) {
				const c = await post('/api/playlists/create', { name: nm }); if (!c.id) continue;
				const body = new URLSearchParams(); body.set('id', c.id); const o = await (await fetch('/api/playlist?id=' + encodeURIComponent(c.id))).json(); body.set('rev', o.rev ?? 0); files.forEach(f => body.append('p[]', f));
				LOG.push(await (await fetch('/api/playlist/add', { method: 'POST', headers: { 'X-BP': '1', 'Content-Type': 'application/x-www-form-urlencoded' }, body: body.toString() })).text());
			}
		};`;
	if (steps) console.log(name, 'steps', await ev(`(async()=>{ ${helpers} ${steps}; return JSON.stringify(LOG) })()`));
	if (steps && steps.includes('makeLists')) { await send('Page.navigate', { url: base + '?shot=' + Date.now() + hash }); await sleep(3500); }
	await sleep(1500);
	const r = await send('Page.captureScreenshot', { format: 'png' });
	fs.writeFileSync(path.join(root, 'docs/img', `web-${name}.png`), Buffer.from(r.result.data, 'base64'));
	console.log('docs/img/web-' + name + '.png', theme);
	ws.close(); chrome.kill(); player.kill();
	await sleep(800);
	for (const d of [state, profile]) try { fs.rmSync(d, { recursive: true, force: true }); } catch {}
}
let n = 0;
for (const s of shots) await run(s, n++);
process.exit(0);
