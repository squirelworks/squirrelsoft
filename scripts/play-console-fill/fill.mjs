#!/usr/bin/env node
/**
 * Local Play Console App content filler via Brave/Chrome CDP.
 *
 * Prefer: connectOverCDP(http://127.0.0.1:9222)
 * One app at a time; anti-loop (max 2 attempts/card); screenshot failures to ./out/
 * If a card cannot be automated, prints EXACT MANUAL click for that card and continues.
 *
 * Usage:
 *   npm install
 *   npm run fill:suica
 *   npm run fill -- --app sound
 *   npm run fill -- --app qr --cards privacy,ads,signin
 *   npm run probe
 */
import { chromium } from 'playwright-core';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

import {
  APPS,
  CDP_URL,
  CONTACT_EMAIL,
  DEFAULT_CARDS,
  DEV_ID,
  MANUAL,
  appContentBase,
} from './config.mjs';
import {
  clickRadioMatching,
  clickStartFor,
  dismissModals,
  inventory,
  makeAttemptGuard,
  makeLogger,
  parseArgs,
  saveOrNext,
  selectRadioByLabel,
  shot,
  waitSettle,
} from './lib/helpers.mjs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const OUT_DIR = path.join(__dirname, 'out');

function ensureOut() {
  fs.mkdirSync(OUT_DIR, { recursive: true });
}

function manualHint(card, app) {
  const m = MANUAL[card];
  return typeof m === 'function' ? m(app) : m || `MANUAL: complete card "${card}" in Console UI`;
}

function baseUrl() {
  return appContentBase(DEV_ID);
}

async function gotoSummary(page, app) {
  const url = `${baseUrl()}/${app.id}/app-content/summary`;
  await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 60000 });
  await waitSettle(page, 2000);
  return page.url();
}

async function gotoCard(page, app, slug) {
  const url = `${baseUrl()}/${app.id}/app-content/${slug}`;
  await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 60000 });
  await waitSettle(page, 2000);
}

/* ---------- card fillers ---------- */

async function fillPrivacy(ctx, app, key) {
  const { page, log, canAttempt } = ctx;
  const k = `${key}:privacy`;
  if (!canAttempt(k)) return 'skipped';
  log(`STEP start ${k}`);
  await gotoCard(page, app, 'privacy-policy');
  if (!page.url().includes('privacy')) {
    await gotoSummary(page, app);
    const opened = await clickStartFor(page, 'Privacy policy');
    if (!opened) {
      log(manualHint('privacy', app));
      await shot(page, OUT_DIR, `${key}_privacy_FAIL`);
      return 'manual';
    }
    await waitSettle(page, 2000);
  }
  const input = page
    .getByRole('textbox', { name: /Privacy policy URL/i })
    .or(page.locator('input[type="url"], input[aria-label*="Privacy" i]'))
    .first();
  try {
    await input.waitFor({ timeout: 15000 });
  } catch {
    log(manualHint('privacy', app));
    await shot(page, OUT_DIR, `${key}_privacy_FAIL`);
    return 'manual';
  }
  await input.fill('');
  await input.fill(app.privacy);
  log(`STEP privacy URL filled: ${app.privacy}`);
  const acted = await saveOrNext(page);
  await shot(page, OUT_DIR, `${key}_privacy`);
  log(`OK ${k} save=${acted}`);
  return 'done';
}

async function fillSignIn(ctx, app, key) {
  const { page, log, canAttempt } = ctx;
  const k = `${key}:signin`;
  if (!canAttempt(k)) return 'skipped';
  log(`STEP start ${k}`);
  await gotoCard(page, app, 'app-access');
  const clicked =
    (await page
      .getByText(/All functionality.*without|available without special access|No.*restricted/i)
      .first()
      .click({ timeout: 8000 })
      .then(() => true)
      .catch(() => false)) ||
    !!(await clickRadioMatching(
      page,
      /All functionality|without special|not restricted/i
    ));
  if (!clicked) {
    log(manualHint('signin', app));
    await shot(page, OUT_DIR, `${key}_signin_FAIL`);
    return 'manual';
  }
  log('STEP signin: selected no-restricted-access');
  await saveOrNext(page);
  await shot(page, OUT_DIR, `${key}_signin`);
  log(`OK ${k}`);
  return 'done';
}

async function fillAds(ctx, app, key) {
  const { page, log, canAttempt } = ctx;
  const k = `${key}:ads`;
  if (!canAttempt(k)) return 'skipped';
  log(`STEP start ${k}`);
  await gotoCard(page, app, 'ads');
  let ok =
    (await selectRadioByLabel(page, /does not contain ads|No, my app does not/i)) ||
    !!(await clickRadioMatching(page, /^No\b|does not contain ads/i));
  if (!ok) {
    await page.getByText(/^No$/).first().click({ timeout: 3000 }).catch(() => {});
    ok = true;
  }
  if (!ok) {
    log(manualHint('ads', app));
    await shot(page, OUT_DIR, `${key}_ads_FAIL`);
    return 'manual';
  }
  log('STEP ads: selected No');
  await saveOrNext(page);
  await shot(page, OUT_DIR, `${key}_ads`);
  log(`OK ${k}`);
  return 'done';
}

async function fillAdvertisingId(ctx, app, key) {
  const { page, log, canAttempt } = ctx;
  const k = `${key}:adid`;
  if (!canAttempt(k)) return 'skipped';
  log(`STEP start ${k}`);
  await gotoCard(page, app, 'advertising-id');
  const body = await page.innerText('body').catch(() => '');
  if (/page (not found|doesn't exist)|404/i.test(body) && !/Advertising ID/i.test(body)) {
    log(`INFO ${k}: Advertising ID card not present (OK to skip)`);
    return 'absent';
  }
  const clicked =
    (await page
      .getByText(/No.*advertising ID|does not use advertising ID|do not use.*advertising/i)
      .first()
      .click({ timeout: 5000 })
      .then(() => true)
      .catch(() => false)) ||
    !!(await clickRadioMatching(page, /^No\b|does not use|don't use|do not use/i));
  if (!clicked) {
    log(manualHint('adid', app));
    await shot(page, OUT_DIR, `${key}_adid_FAIL`);
    return 'manual';
  }
  log('STEP adid: selected No');
  await saveOrNext(page);
  await shot(page, OUT_DIR, `${key}_adid`);
  log(`OK ${k}`);
  return 'done';
}

async function fillGovernment(ctx, app, key) {
  const { page, log, canAttempt } = ctx;
  const k = `${key}:gov`;
  if (!canAttempt(k)) return 'skipped';
  log(`STEP start ${k}`);
  await gotoCard(page, app, 'government-apps');
  const clicked =
    !!(await clickRadioMatching(page, /^No\b/)) ||
    (await selectRadioByLabel(page, /^No$/));
  if (!clicked) {
    log(manualHint('government', app));
    await shot(page, OUT_DIR, `${key}_gov_FAIL`);
    return 'manual';
  }
  log('STEP government: No');
  await saveOrNext(page);
  await shot(page, OUT_DIR, `${key}_gov`);
  log(`OK ${k}`);
  return 'done';
}

async function fillFinancial(ctx, app, key) {
  const { page, log, canAttempt } = ctx;
  const k = `${key}:fin`;
  if (!canAttempt(k)) return 'skipped';
  log(`STEP start ${k}`);
  await gotoCard(page, app, 'financial-features');
  const none = page
    .getByText(/does not provide any financial|No financial features|None of the above|My app does not/i)
    .first();
  if (await none.count()) {
    await none.scrollIntoViewIfNeeded();
    await none.click({ timeout: 8000 });
    log('STEP financial: none selected');
  } else {
    const alt = await page
      .locator('label, mat-checkbox, mat-radio-button')
      .filter({ hasText: /does not provide|None|No financial/i })
      .first()
      .click({ timeout: 8000 })
      .then(() => true)
      .catch(() => false);
    if (!alt) {
      log(manualHint('financial', app));
      await shot(page, OUT_DIR, `${key}_fin_FAIL`);
      return 'manual';
    }
  }
  await waitSettle(page, 800);
  for (let guard = 0; guard < 5; guard++) {
    const next = page.getByRole('button', { name: /^Next$/ }).first();
    if (
      (await next.count()) &&
      (await next.isEnabled().catch(() => false))
    ) {
      await next.click();
      await waitSettle(page, 1500);
      const none2 = page
        .getByText(/does not provide any financial|None of the above|My app does not/i)
        .first();
      if (await none2.count()) await none2.click().catch(() => {});
      continue;
    }
    break;
  }
  await saveOrNext(page);
  await shot(page, OUT_DIR, `${key}_fin`);
  log(`OK ${k}`);
  return 'done';
}

async function fillHealth(ctx, app, key) {
  const { page, log, canAttempt } = ctx;
  const k = `${key}:health`;
  if (!canAttempt(k)) return 'skipped';
  log(`STEP start ${k}`);
  await gotoCard(page, app, 'health');
  const none = page
    .getByText(/My app does not have any health features/i)
    .first();
  if (!(await none.count())) {
    log(manualHint('health', app));
    await shot(page, OUT_DIR, `${key}_health_FAIL`);
    return 'manual';
  }
  await none.scrollIntoViewIfNeeded();
  await none.click({ timeout: 8000 });
  log('STEP health: none selected');
  await waitSettle(page, 800);
  for (let guard = 0; guard < 4; guard++) {
    const next = page.getByRole('button', { name: /^Next$/ }).first();
    if (
      (await next.count()) &&
      (await next.isEnabled().catch(() => false))
    ) {
      await next.click();
      await waitSettle(page, 1500);
      continue;
    }
    break;
  }
  await saveOrNext(page);
  await shot(page, OUT_DIR, `${key}_health`);
  log(`OK ${k}`);
  return 'done';
}

async function fillTargetAudience(ctx, app, key) {
  const { page, log, canAttempt } = ctx;
  const k = `${key}:audience`;
  if (!canAttempt(k)) return 'skipped';
  log(`STEP start ${k}`);
  await gotoCard(page, app, 'target-audience-content');
  const age = page.getByText(/18 and over|18\+|18 years/i).first();
  if (await age.count()) {
    await age.scrollIntoViewIfNeeded();
    await age.click();
    log('STEP audience: 18+ selected');
  } else {
    const ok = await page
      .locator('mat-checkbox, label, [role="checkbox"]')
      .filter({ hasText: /18/ })
      .first()
      .click()
      .then(() => true)
      .catch(() => false);
    if (!ok) {
      log(manualHint('audience', app));
      await shot(page, OUT_DIR, `${key}_audience_FAIL`);
      return 'manual';
    }
  }
  await waitSettle(page, 800);
  for (let guard = 0; guard < 8; guard++) {
    for (const re of [
      /Not designed for families/i,
      /does not appeal primarily to children/i,
      /^No$/i,
    ]) {
      const el = page.getByText(re).first();
      if (
        (await el.count()) &&
        (await el.isVisible().catch(() => false))
      ) {
        await el.click().catch(() => {});
      }
    }
    await clickRadioMatching(
      page,
      /^No\b|not designed for families|does not/i
    );
    const next = page.getByRole('button', { name: /^Next$/ }).first();
    if (
      (await next.count()) &&
      (await next.isEnabled().catch(() => false))
    ) {
      await next.click();
      await waitSettle(page, 1500);
      continue;
    }
    const save = page.getByRole('button', { name: /^Save$|^Submit$/ }).first();
    if (
      (await save.count()) &&
      (await save.isEnabled().catch(() => false))
    ) {
      await save.click();
      await waitSettle(page, 1500);
      break;
    }
    break;
  }
  await saveOrNext(page);
  await shot(page, OUT_DIR, `${key}_audience`);
  log(`OK ${k}`);
  return 'done';
}

async function fillDataSafety(ctx, app, key) {
  const { page, log, canAttempt } = ctx;
  const k = `${key}:datasafety`;
  if (!canAttempt(k)) return 'skipped';
  log(`STEP start ${k}`);
  await gotoCard(page, app, 'data-safety');
  let progressed = false;
  for (let guard = 0; guard < 12; guard++) {
    await page
      .getByText(/No.*collect|does not collect|doesn't collect|Don't collect|No data/i)
      .first()
      .click({ timeout: 2000 })
      .catch(() => {});
    await clickRadioMatching(
      page,
      /^No\b|does not collect|Don't collect|No data/i
    );
    const next = page.getByRole('button', { name: /^Next$|^Continue$/ }).first();
    if (
      (await next.count()) &&
      (await next.isEnabled().catch(() => false))
    ) {
      await next.click();
      await waitSettle(page, 1200);
      progressed = true;
      continue;
    }
    const save = page
      .getByRole('button', { name: /^Save$|^Submit$|^Confirm$/ })
      .first();
    if (
      (await save.count()) &&
      (await save.isEnabled().catch(() => false))
    ) {
      await save.click();
      await waitSettle(page, 1500);
      progressed = true;
      break;
    }
    break;
  }
  const acted = await saveOrNext(page);
  if (!progressed && !acted) {
    log(manualHint('datasafety', app));
    await shot(page, OUT_DIR, `${key}_datasafety_FAIL`);
    return 'manual';
  }
  await shot(page, OUT_DIR, `${key}_datasafety`);
  log(`OK ${k}`);
  return 'done';
}

async function fillContentRating(ctx, app, key) {
  const { page, log, canAttempt } = ctx;
  const k = `${key}:iarc`;
  if (!canAttempt(k)) return 'skipped';
  log(`STEP start ${k} (IARC — resilient / best-effort)`);
  await gotoCard(page, app, 'content-rating');
  const body0 = await page.innerText('body').catch(() => '');
  if (/content rating|IARC|questionnaire|Start/i.test(body0) === false) {
    log(`WARN ${k}: unexpected page — UI may differ`);
    log(manualHint('iarc', app));
    await shot(page, OUT_DIR, `${key}_iarc_FAIL`);
    return 'manual';
  }

  await page
    .getByRole('button', { name: /Start questionnaire|Start|Continue/i })
    .first()
    .click({ timeout: 5000 })
    .catch(() => {});
  await waitSettle(page, 1500);

  const email = page
    .getByRole('textbox', { name: /email/i })
    .or(page.locator('input[type="email"]'))
    .first();
  if (await email.count()) {
    await email.fill(CONTACT_EMAIL);
    log(`STEP iarc: email ${CONTACT_EMAIL}`);
  } else {
    log('WARN iarc: email field not found (continuing)');
  }
  await saveOrNext(page);

  const catClicked =
    (await page
      .getByText(app.iarcCategoryHint)
      .first()
      .click({ timeout: 5000 })
      .then(() => true)
      .catch(() => false)) ||
    (await page
      .locator('mat-radio-button, label')
      .filter({ hasText: app.iarcCategoryHint })
      .first()
      .click({ timeout: 5000 })
      .then(() => true)
      .catch(() => false));
  if (catClicked) log('STEP iarc: category selected');
  else log('WARN iarc: category control not matched — continuing with No answers');
  await saveOrNext(page);

  let answered = 0;
  for (let guard = 0; guard < 25; guard++) {
    const body = await page.innerText('body').catch(() => '');
    if (
      /Your ratings|Ratings calculated|Submit ratings|content rating.*complete/i.test(
        body
      )
    ) {
      await page
        .getByRole('button', { name: /Submit|Save|Apply|Confirm/i })
        .first()
        .click({ timeout: 5000 })
        .catch(() => {});
      await waitSettle(page, 2000);
      log('STEP iarc: submitted ratings');
      break;
    }
    const radios = page.locator('mat-radio-button, [role="radio"]');
    const n = await radios.count();
    let clickedNo = false;
    for (let i = 0; i < n; i++) {
      const t = (await radios.nth(i).innerText().catch(() => '')).trim();
      if (t === 'No' || /^No\b/.test(t)) {
        await radios.nth(i).click().catch(() => {});
        clickedNo = true;
        answered++;
      }
    }
    await page.getByText(/^No$/).first().click({ timeout: 800 }).catch(() => {});
    const next = page
      .getByRole('button', { name: /^Next$|^Continue$|^Save$/ })
      .first();
    if (
      (await next.count()) &&
      (await next.isEnabled().catch(() => false))
    ) {
      await next.click();
      await waitSettle(page, 1200);
      continue;
    }
    if (!clickedNo) {
      log(
        `WARN iarc: no more No radios (answered≈${answered}); UI may differ — check screenshot`
      );
      break;
    }
    await waitSettle(page, 400);
  }
  await saveOrNext(page);
  await shot(page, OUT_DIR, `${key}_iarc`);
  log(`OK ${k} (best-effort; verify Console if questionnaire UI differed)`);
  return 'done';
}

async function fillTesters(ctx, app, key) {
  const { page, log, canAttempt } = ctx;
  const k = `${key}:testers`;
  if (!canAttempt(k)) return 'skipped';
  log(`STEP start ${k}`);
  // Internal testing testers page
  const url = `${baseUrl()}/${app.id}/tracks/internal-testing`;
  await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 60000 });
  await waitSettle(page, 2500);

  // Try Testers tab / create email list
  await page
    .getByRole('tab', { name: /Testers/i })
    .first()
    .click({ timeout: 5000 })
    .catch(() => {});
  await page
    .getByText(/^Testers$/i)
    .first()
    .click({ timeout: 3000 })
    .catch(() => {});
  await waitSettle(page, 1000);

  // Create email list / add email
  await page
    .getByRole('button', { name: /Create email list|Create list|Add email|Edit list/i })
    .first()
    .click({ timeout: 5000 })
    .catch(() => {});
  await waitSettle(page, 1000);

  const emailBox = page
    .getByRole('textbox', { name: /email|list name|Add email/i })
    .or(page.locator('textarea, input[type="email"], input[aria-label*="email" i]'))
    .first();

  let filled = false;
  if (await emailBox.count()) {
    // If a list-name field appears first, name it then find email field
    const label = await emailBox.getAttribute('aria-label').catch(() => '');
    if (/list name|name of/i.test(label || '')) {
      await emailBox.fill('squirrelworks-internal');
      await waitSettle(page, 400);
    }
    const boxes = page.locator(
      'textarea, input[type="email"], input[aria-label*="email" i], input[placeholder*="email" i]'
    );
    const n = await boxes.count();
    for (let i = 0; i < n; i++) {
      const el = boxes.nth(i);
      const ph = (
        (await el.getAttribute('placeholder').catch(() => '')) ||
        (await el.getAttribute('aria-label').catch(() => '')) ||
        ''
      ).toLowerCase();
      if (/email|comma|enter/i.test(ph) || n === 1) {
        await el.click().catch(() => {});
        await el.fill(CONTACT_EMAIL).catch(async () => {
          await el.type(CONTACT_EMAIL).catch(() => {});
        });
        filled = true;
        break;
      }
    }
  }

  if (!filled) {
    // Try any visible textarea
    const ta = page.locator('textarea').first();
    if (await ta.count()) {
      await ta.fill(CONTACT_EMAIL).catch(() => {});
      filled = true;
    }
  }

  if (filled) {
    log(`STEP testers: entered ${CONTACT_EMAIL}`);
    await saveOrNext(page);
    await shot(page, OUT_DIR, `${key}_testers`);
    log(`OK ${k}`);
    return 'done';
  }

  log(manualHint('testers', app));
  await shot(page, OUT_DIR, `${key}_testers_FAIL`);
  return 'manual';
}

const FILLERS = {
  privacy: fillPrivacy,
  signin: fillSignIn,
  ads: fillAds,
  adid: fillAdvertisingId,
  iarc: fillContentRating,
  audience: fillTargetAudience,
  datasafety: fillDataSafety,
  government: fillGovernment,
  financial: fillFinancial,
  health: fillHealth,
  testers: fillTesters,
};

async function fillApp(ctx, key, app, cards) {
  const { page, log, status } = ctx;
  status.apps[key] = status.apps[key] || { pkg: app.pkg, name: app.name, results: {} };
  log(`==== APP ${key} ${app.pkg} (${app.name}) cards=${cards.join(',')} ====`);
  for (const card of cards) {
    const fn = FILLERS[card];
    if (!fn) {
      log(`WARN unknown card: ${card}`);
      status.apps[key].results[card] = 'unknown';
      continue;
    }
    try {
      const r = await fn(ctx, app, key);
      status.apps[key].results[card] = r;
      log(`RESULT ${key}:${card} → ${r}`);
    } catch (e) {
      log(`ERR ${key}:${card} ${e.message}`);
      status.apps[key].results[card] = `error: ${e.message}`;
      await shot(page, OUT_DIR, `${key}_${card}_ERR`);
      log(manualHint(card, app));
    }
    await dismissModals(page);
  }
  await gotoSummary(page, app);
  const inv = await inventory(page);
  status.apps[key].finalNeed = inv.need;
  status.apps[key].finalActioned = inv.actioned;
  status.apps[key].finalSnippet = (inv.textSnippet || '').slice(0, 800);
  await shot(page, OUT_DIR, `${key}_overview_final`);
  log(`==== APP ${key} done need=${inv.need} actioned=${inv.actioned} ====`);
}

async function connectPage(cdpUrl, log) {
  log(`Connecting CDP ${cdpUrl}`);
  const browser = await chromium.connectOverCDP(cdpUrl);
  const contexts = browser.contexts();
  if (!contexts.length) throw new Error('No browser contexts on CDP — is Brave running with --remote-debugging-port=9222?');
  const pages = contexts.flatMap((c) => c.pages());
  let page =
    pages.find((p) => /play\.google\.com/.test(p.url())) ||
    pages.find((p) => /google\.com/.test(p.url())) ||
    pages[0];
  if (!page) {
    page = await contexts[0].newPage();
  }
  log(`Attached page: ${page.url()}`);
  return { browser, page };
}

async function probe(page, log) {
  const info = {
    url: page.url(),
    title: await page.title().catch(() => ''),
    cookiesHint: 'see DevTools',
  };
  const body = await page.innerText('body').catch(() => '');
  const onAccounts =
    /accounts\.google\.com/i.test(info.url) ||
    /Sign in - Google Accounts/i.test(info.title || '');
  info.looksLoggedIn =
    /play\.google\.com\/console/i.test(info.url) &&
    /Play Console|All apps|Dashboard|App content|Policy/i.test(body);
  info.looksSignIn =
    onAccounts ||
    ((/Sign in|Email or phone|accountchooser|Choose an account/i.test(body) ||
      /Sign in|accountchooser/i.test(info.url)) &&
      !info.looksLoggedIn);
  log(`PROBE ${JSON.stringify(info)}`);
  await shot(page, OUT_DIR, 'probe');
  fs.writeFileSync(path.join(OUT_DIR, 'probe.json'), JSON.stringify(info, null, 2));
  return info;
}

function printHelp() {
  console.log(`Play Console local filler (CDP)

Usage:
  node fill.mjs [--app suica|sound|qr] [--cards privacy,ads,...] [--cdp URL] [--probe]

Apps: ${Object.keys(APPS).join(', ')}
Cards: ${DEFAULT_CARDS.join(', ')}

See README.md for Snap Brave start commands.
`);
}

async function main() {
  const args = parseArgs(process.argv);
  if (args.help) {
    printHelp();
    process.exit(0);
  }
  ensureOut();
  const { log, lines } = makeLogger(OUT_DIR);
  const canAttempt = makeAttemptGuard(log, args.maxAttempts);
  const status = {
    started: new Date().toISOString(),
    cdp: args.cdp || CDP_URL,
    apps: {},
  };
  const ctx = { page: null, log, canAttempt, status };

  const cdp = args.cdp || CDP_URL;
  let browser;
  try {
    const conn = await connectPage(cdp, log);
    browser = conn.browser;
    ctx.page = conn.page;
  } catch (e) {
    log(`FATAL CDP connect failed: ${e.message}`);
    log(
      'Start Brave first, e.g.:\n  /snap/bin/brave --remote-debugging-port=9222 --user-data-dir=$HOME/snap/brave/current/.config/BraveSoftware/Brave-Browser'
    );
    process.exit(1);
  }

  const info = await probe(ctx.page, log);
  if (args.probe) {
    status.finished = new Date().toISOString();
    fs.writeFileSync(path.join(OUT_DIR, 'status.json'), JSON.stringify(status, null, 2));
    process.exit(info.looksLoggedIn ? 0 : 2);
  }
  if (info.looksSignIn) {
    log('LOGIN required — sign into Play Console in the Brave window, then re-run.');
    status.login = 'required';
    fs.writeFileSync(path.join(OUT_DIR, 'status.json'), JSON.stringify(status, null, 2));
    process.exit(2);
  }
  status.login = 'ok';

  if (args.dryRun) {
    log('Dry run — probe only');
    process.exit(0);
  }

  const order = args.app
    ? [args.app]
    : ['suica', 'sound', 'qr'];
  for (const key of order) {
    const app = APPS[key];
    if (!app) {
      log(`ERROR unknown app key: ${key}`);
      continue;
    }
    const cards = args.cards || DEFAULT_CARDS;
    // One app at a time (sequential)
    await fillApp(ctx, key, app, cards);
  }

  status.finished = new Date().toISOString();
  status.logTail = lines.slice(-80);
  fs.writeFileSync(path.join(OUT_DIR, 'status.json'), JSON.stringify(status, null, 2));
  log('ALL DONE — review ./out/status.json and screenshots');
  // Do not close the user's Brave session
  process.exit(0);
}

main().catch((e) => {
  console.error(e);
  try {
    fs.writeFileSync(
      path.join(OUT_DIR, 'fatal.json'),
      JSON.stringify({ error: String(e), stack: e.stack }, null, 2)
    );
  } catch {
    /* ignore */
  }
  process.exit(1);
});
