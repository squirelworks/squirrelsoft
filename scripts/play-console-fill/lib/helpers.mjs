import fs from 'fs';
import path from 'path';

/**
 * Shared Playwright helpers for Play Console UI.
 * Prefer getByRole / getByText; never assume stable CSS class names.
 */

export function makeLogger(outDir) {
  const lines = [];
  const logPath = path.join(outDir, 'fill.log');
  function log(...args) {
    const line = `${new Date().toISOString()} ${args.join(' ')}`;
    console.log(line);
    lines.push(line);
    try {
      fs.appendFileSync(logPath, line + '\n');
    } catch {
      /* ignore */
    }
  }
  return { log, lines, logPath };
}

export function makeAttemptGuard(log, max = 2) {
  const attempts = Object.create(null);
  return function canAttempt(key) {
    attempts[key] = (attempts[key] || 0) + 1;
    if (attempts[key] > max) {
      log(`SKIP ${key} (anti-loop: max ${max} attempts)`);
      return false;
    }
    log(`ATTEMPT ${key} #${attempts[key]}/${max}`);
    return true;
  };
}

export async function shot(page, outDir, name) {
  const file = path.join(outDir, `${name}.png`);
  try {
    await page.screenshot({ path: file, fullPage: false });
    return file;
  } catch (e) {
    return null;
  }
}

export async function dismissModals(page) {
  for (const label of [
    'Got it',
    'Close',
    'Dismiss',
    'Not now',
    'Cancel',
    'OK',
    'Continue',
  ]) {
    const b = page.getByRole('button', { name: label, exact: true });
    if (
      (await b.count()) &&
      (await b.first().isVisible().catch(() => false))
    ) {
      await b.first().click({ timeout: 2000 }).catch(() => {});
      await page.waitForTimeout(300);
    }
  }
  const closeX = page.locator(
    '[aria-label="Close"], button:has-text("Close")'
  );
  if (await closeX.count()) {
    await closeX.first().click({ timeout: 1500 }).catch(() => {});
  }
}

export async function waitSettle(page, ms = 1200) {
  await page.waitForTimeout(ms);
  await dismissModals(page);
}

export async function saveOrNext(page) {
  await dismissModals(page);
  for (const name of [
    'Save',
    'Submit',
    'Next',
    'Save and continue',
    'Confirm',
    'Calculate',
    'Apply',
    'Continue',
  ]) {
    const b = page.getByRole('button', { name, exact: true });
    if (
      (await b.count()) &&
      (await b.first().isEnabled().catch(() => false))
    ) {
      await b.first().click({ timeout: 5000 }).catch(() => {});
      await waitSettle(page, 1500);
      await dismissModals(page);
      return name;
    }
  }
  for (const name of [/Save/, /Submit/, /Next/, /Calculate/, /Continue/]) {
    const b = page.getByRole('button', { name }).first();
    if (
      (await b.count()) &&
      (await b.isEnabled().catch(() => false))
    ) {
      await b.click({ timeout: 5000 }).catch(() => {});
      await waitSettle(page, 1500);
      return 'fuzzy';
    }
  }
  return null;
}

export async function selectRadioByLabel(page, labelRe) {
  const radio = page.getByRole('radio', { name: labelRe }).first();
  if (await radio.count()) {
    await radio.click({ timeout: 5000 });
    return true;
  }
  const lab = page.getByText(labelRe).first();
  if (await lab.count()) {
    await lab.scrollIntoViewIfNeeded();
    await lab.click({ timeout: 5000 });
    return true;
  }
  return false;
}

/** Click first mat-radio / role=radio whose visible text matches. */
export async function clickRadioMatching(page, re) {
  const radios = page.locator('mat-radio-button, [role="radio"]');
  const n = await radios.count();
  for (let i = 0; i < n; i++) {
    const t = (await radios.nth(i).innerText().catch(() => '')).trim();
    if (re.test(t)) {
      await radios.nth(i).click({ timeout: 5000 });
      return t;
    }
  }
  return null;
}

export async function clickStartFor(page, cardTitle) {
  const heading = page.getByRole('heading', { name: cardTitle }).first();
  if (await heading.count()) {
    await heading.scrollIntoViewIfNeeded();
  } else {
    await page
      .getByText(cardTitle, { exact: true })
      .first()
      .scrollIntoViewIfNeeded()
      .catch(() => {});
  }
  const card = page
    .locator('section, article, div')
    .filter({ hasText: cardTitle })
    .filter({
      has: page.getByText(/Start declaration|Manage|Start questionnaire|Start/i),
    })
    .first();
  const start = card
    .getByText(/Start declaration|Manage declaration|Start questionnaire|Manage/i)
    .first();
  if (await start.count()) {
    await start.click({ timeout: 8000 });
    return true;
  }
  const alt = page
    .getByRole('link', { name: /Start declaration|Manage|Start questionnaire/i })
    .first();
  if (await alt.count()) {
    await alt.click();
    return true;
  }
  const btn = page
    .getByRole('button', { name: /Start declaration|Manage|Start questionnaire|Start/i })
    .first();
  if (await btn.count()) {
    await btn.click();
    return true;
  }
  return false;
}

export async function inventory(page) {
  return page.evaluate(() => {
    const t = document.body.innerText || '';
    const need = (t.match(/Need attention\s*\((\d+)\)/) || [])[1] || null;
    const actioned = (t.match(/Actioned\s*\((\d+)\)/) || [])[1] || null;
    return {
      need,
      actioned,
      hasStart: /Start declaration|Start questionnaire/i.test(t),
      textSnippet: t.slice(0, 3000),
    };
  });
}

export function parseArgs(argv) {
  const out = {
    app: null, // null = all in order suica → sound → qr
    cards: null,
    cdp: null,
    probe: false,
    dryRun: false,
    maxAttempts: 2,
  };
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--app' && argv[i + 1]) out.app = argv[++i];
    else if (a === '--cards' && argv[i + 1])
      out.cards = argv[++i].split(',').map((s) => s.trim()).filter(Boolean);
    else if (a === '--cdp' && argv[i + 1]) out.cdp = argv[++i];
    else if (a === '--probe') out.probe = true;
    else if (a === '--dry-run') out.dryRun = true;
    else if (a === '--max-attempts' && argv[i + 1])
      out.maxAttempts = Number(argv[++i]) || 2;
    else if (a === '--help' || a === '-h') out.help = true;
  }
  return out;
}
