const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const page = fs.readFileSync('main/web/adventure-editor.html', 'utf8');
assert.match(page, /id="source"/);
assert.match(page, /id="browse"/);
assert.match(page, /id="validate"/);
assert.match(page, /id="save"/);
assert.match(page, /kommentarer och syntax/i);
assert.match(page, /\/api\/adventure\/validate/);
assert.match(page, /\/api\/fs\/download/);
assert.match(page, /\/api\/fs\/upload/);
assert.match(page, /overwrite:'1'/);
assert.match(page, /\/otr\/adventure\//);

assert.match(page, /<script src="\/adventure-model\.js"><\/script>/);
assert.match(page, /id="map-canvas"/);
assert.match(page, /id="lg-add"/);
assert.match(page, /data-tool="move"/);
assert.match(page, /data-tool="dark"/);
assert.match(page, /id="nf-create"/);
assert.match(page, /id="am-add"/);
assert.match(page, /id="rs-fill"/);
assert.match(page, /id="zoom"/);
assert.match(page, /\/api\/files\/mkdir/);
assert.doesNotMatch(page, /1–80/);
assert.match(fs.readFileSync('tools/embed_web.py', 'utf8'), /"\/adventure-model\.js"/);

const script = page.match(/<script\b(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script\s*>/i)[1];
new vm.Script(script);
const server = fs.readFileSync('main/app_web_adventure_editor.cpp', 'utf8');
assert.match(server, /request->content_len > MAX_BODY/);
assert.match(server, /text\/plain/);
assert.match(server, /query\.adminAllowed\(\)/);
assert.match(server, /Adv::validateText/);
assert.match(server, /\/api\/adventure\/validate/);
console.log('Adventure editor page and production-validation route checks passed');
