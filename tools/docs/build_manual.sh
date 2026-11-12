#!/bin/sh
# Builds docs/HUSH_Manual.pdf from docs/HUSH_Manual.md (pandoc + Chromium via Node Playwright).
set -e
cd "$(dirname "$0")/../.."
pandoc docs/HUSH_Manual.md -s --toc --toc-depth=1 -c ../tools/docs/manual.css -o docs/HUSH_Manual.html --metadata lang=en
node -e "
const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage();
  await p.goto('file://' + process.cwd() + '/docs/HUSH_Manual.html', { waitUntil: 'load' });
  await p.pdf({ path: 'docs/HUSH_Manual.pdf', format: 'A5', printBackground: true, displayHeaderFooter: true,
    headerTemplate: '<span></span>', footerTemplate: '<div style=\"font-size:7pt;width:100%;text-align:center;color:#889\">HUSH · <span class=pageNumber></span></div>',
    margin: { top: '14mm', bottom: '16mm', left: '13mm', right: '13mm' } });
  await b.close();
})();"
rm -f docs/HUSH_Manual.html
echo "docs/HUSH_Manual.pdf written"
