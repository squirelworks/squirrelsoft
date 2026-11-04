// Render the store-graphic SVGs to PNG with the preinstalled Chromium.
// usage: node render.js <in.svg> <out.png> [<in.svg> <out.png> ...]
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const browser = await chromium.launch();
  const args = process.argv.slice(2);
  for (let i = 0; i < args.length; i += 2) {
    const svg = fs.readFileSync(args[i], 'utf8');
    const w = +svg.match(/width="(\d+)"/)[1], h = +svg.match(/height="(\d+)"/)[1];
    const page = await browser.newPage({ viewport: { width: w, height: h }, deviceScaleFactor: 1 });
    await page.setContent(`<html><body style="margin:0">${svg}</body></html>`);
    await page.screenshot({ path: args[i + 1], omitBackground: false, clip: { x: 0, y: 0, width: w, height: h } });
    await page.close();
    console.log(args[i + 1], `${w}x${h}`);
  }
  await browser.close();
})();
