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

function bgraToRgb565(bgra) {
  const b = (bgra >> 24) & 0xFF;
  const g = (bgra >> 16) & 0xFF;
  const r = (bgra >> 8) & 0xFF;
  const a = bgra & 0xFF;

  if (a < 128) return 0x0000;

  const r5 = (r >> 3) & 0x1F;
  const g6 = (g >> 2) & 0x3F;
  const b5 = (b >> 3) & 0x1F;

  return (r5 << 11) | (g6 << 5) | b5;
}

function buildAnimationRaw() {
  const rawPath = path.join(outputDir, `${animation.ident}.rgb565`);
  const input = path.join(sourceDir, animation.file);
  const rgbaPath = path.join(outputDir, `${animation.ident}_rgba.raw`);

  runFfmpeg([
    '-y',
    '-i', input,
    '-vf', `select='not(mod(n\\,${animation.selectEvery}))',setpts=N/15/TB,scale=${animation.width}:${animation.height}:flags=neighbor:force_original_aspect_ratio=decrease`,
    '-pixel_format', 'rgba',
    '-f', 'rawvideo',
    rgbaPath,
  ]);

  const rgba = fs.readFileSync(rgbaPath);
  const values = [];
  for (let i = 0; i < rgba.length; i += 4) {
    const pixel = (rgba[i] << 24) | (rgba[i+1] << 16) | (rgba[i+2] << 8) | rgba[i+3];
    values.push(bgraToRgb565(pixel));
  }

  fs.unlinkSync(rgbaPath);

  const frameSize = animation.width * animation.height;
  const frameCount = Math.floor(values.length / frameSize);
  return { rawPath, values, frameCount };
}

function buildPosterRaw(poster, width, height) {
  const rawPath = path.join(outputDir, `${poster.ident}.rgb565`);
  const pngPath = path.join(outputDir, `${poster.ident}.png`);
  const rgbaPath = path.join(outputDir, `${poster.ident}_rgba.raw`);
  const input = path.join(sourceDir, poster.file);

  runFfmpeg([
    '-y',
    '-i', input,
    '-vf', `select='eq(n,${poster.frame})'`,
    '-vframes', '1',
    pngPath,
  ]);

  runFfmpeg([
    '-y',
    '-i', pngPath,
    '-pixel_format', 'rgba',
    '-f', 'rawvideo',
    rgbaPath,
  ]);

  const rgba = fs.readFileSync(rgbaPath);
  const values = [];
  for (let i = 0; i < rgba.length; i += 4) {
    const pixel = (rgba[i] << 24) | (rgba[i+1] << 16) | (rgba[i+2] << 8) | rgba[i+3];
    values.push(bgraToRgb565(pixel));
  }

  fs.unlinkSync(rgbaPath);

  return { rawPath, values };
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
