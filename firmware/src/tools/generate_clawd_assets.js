#!/usr/bin/env node

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const projectRoot = path.resolve(__dirname, '..', '..');
const outputDir = path.join(projectRoot, 'assets_clawd_generated');
const sourceDir = '/Users/mtorregrosadev/Documents/GitHub/clawd-on-desk/assets/gif';
const ffmpeg = process.env.FFMPEG || '/opt/homebrew/bin/ffmpeg';

const animation = {
  name: 'Headphones Groove',
  ident: 'headphones_groove',
  file: 'clawd-headphones-groove.gif',
  width: 160,
  height: 160,
  frameDelayMs: 83,
  selectEvery: 4,
};

const posters = [
  { name: 'Juggling', ident: 'juggling', file: 'clawd-juggling.gif', frame: 20 },
  { name: 'Happy', ident: 'happy', file: 'clawd-happy.gif', frame: 18 },
  { name: 'Idle', ident: 'idle', file: 'clawd-idle.gif', frame: 14 },
  { name: 'Thinking', ident: 'thinking', file: 'clawd-thinking.gif', frame: 16 },
];

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function runFfmpeg(args) {
  execFileSync(ffmpeg, args, { stdio: 'inherit' });
}

function toHex16(value) {
  return `0x${value.toString(16).toUpperCase().padStart(4, '0')}`;
}

function writeArray(out, name, values) {
  out.push(`static const uint16_t ${name}[] = {\n`);
  for (let i = 0; i < values.length; i += 12) {
    const chunk = values.slice(i, i + 12).map(toHex16).join(', ');
    out.push(`    ${chunk},\n`);
  }
  out.push('};\n\n');
}

function rawToU16(filePath) {
  const raw = fs.readFileSync(filePath);
  const values = new Array(raw.length / 2);
  for (let i = 0; i < raw.length; i += 2) {
    values[i / 2] = raw.readUInt16LE(i);
  }
  return values;
}

function buildAnimationRaw() {
  const rawPath = path.join(outputDir, `${animation.ident}.rgb565`);
  const input = path.join(sourceDir, animation.file);
  const filter = [
    `[0:v]select='not(mod(n\\,${animation.selectEvery}))',setpts=N/15/TB,scale=${animation.width}:${animation.height}:flags=neighbor:force_original_aspect_ratio=decrease[fg]`,
    `[1:v][fg]overlay=(W-w)/2:(H-h)/2:shortest=1:eof_action=endall,format=rgb565le[out]`,
  ].join(';');

  runFfmpeg([
    '-y',
    '-i', input,
    '-f', 'lavfi',
    '-i', `color=c=black:s=${animation.width}x${animation.height}:r=15`,
    '-filter_complex', filter,
    '-map', '[out]',
    '-fps_mode', 'vfr',
    '-f', 'rawvideo',
    rawPath,
  ]);

  const values = rawToU16(rawPath);
  const frameSize = animation.width * animation.height;
  const frameCount = Math.floor(values.length / frameSize);
  return { rawPath, values, frameCount };
}

function buildPosterRaw(poster, width, height) {
  const rawPath = path.join(outputDir, `${poster.ident}.rgb565`);
  const input = path.join(sourceDir, poster.file);
  const filter = [
    `[0:v]select='eq(n\\,${poster.frame})',scale=${width}:${height}:flags=neighbor:force_original_aspect_ratio=decrease[fg]`,
    `[1:v][fg]overlay=(W-w)/2:(H-h)/2:shortest=1,format=rgb565le[out]`,
  ].join(';');

  runFfmpeg([
    '-y',
    '-i', input,
    '-f', 'lavfi',
    '-i', `color=c=black:s=${width}x${height}:r=15`,
    '-filter_complex', filter,
    '-map', '[out]',
    '-frames:v', '1',
    '-f', 'rawvideo',
    rawPath,
  ]);

  return { rawPath, values: rawToU16(rawPath) };
}

function generate() {
  ensureDir(outputDir);

  const anim = buildAnimationRaw();
  const posterData = posters.map((poster) => ({
    ...poster,
    ...buildPosterRaw(poster, animation.width, animation.height),
  }));

  const header = `#pragma once

#include <stdint.h>

struct ClawdAnimationAsset {
    const char* name;
    uint16_t width;
    uint16_t height;
    uint16_t frame_count;
    uint16_t frame_delay_ms;
    const uint16_t* frames;
};

struct ClawdPosterAsset {
    const char* name;
    uint16_t width;
    uint16_t height;
    const uint16_t* pixels;
};

extern const ClawdAnimationAsset kClawdSplashAnimation;
extern const ClawdPosterAsset kClawdGalleryPosters[];
extern const uint8_t kClawdGalleryPosterCount;
`;

  fs.writeFileSync(path.join(projectRoot, 'src', 'clawd_assets.h'), header);

  const out = [];
  out.push('#include "clawd_assets.h"\n\n');
  writeArray(out, `kClawdAnim_${animation.ident}`, anim.values);

  for (const poster of posterData) {
    writeArray(out, `kClawdPoster_${poster.ident}`, poster.values);
  }

  out.push(`const ClawdAnimationAsset kClawdSplashAnimation = {"${animation.name}", ${animation.width}, ${animation.height}, ${anim.frameCount}, ${animation.frameDelayMs}, kClawdAnim_${animation.ident}};\n\n`);
  out.push('const ClawdPosterAsset kClawdGalleryPosters[] = {\n');
  for (const poster of posterData) {
    out.push(`    {"${poster.name}", ${animation.width}, ${animation.height}, kClawdPoster_${poster.ident}},\n`);
  }
  out.push('};\n\n');
  out.push(`const uint8_t kClawdGalleryPosterCount = ${posterData.length};\n`);

  fs.writeFileSync(path.join(projectRoot, 'src', 'clawd_assets.cpp'), out.join(''));
}

generate();
