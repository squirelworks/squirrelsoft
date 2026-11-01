/** Play Console App content fill config for squirrelsoft apps. */

export const CDP_URL = process.env.PLAY_CDP_URL || 'http://127.0.0.1:9222';

/** Developer account id from Play Console URL …/developers/{id}/… */
export const DEV_ID =
  process.env.PLAY_DEV_ID || '5966793739232059385';

export const CONTACT_EMAIL = 'squirelworks@gmail.com';

export const APPS = {
  suica: {
    key: 'suica',
    id: '4973136291278648754',
    pkg: 'works.squirrel.suica',
    name: 'Suica Reader',
    privacy:
      'https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-suica.html',
    iarcCategoryHint: /Utility|Tools|Productivity|Application/i,
  },
  sound: {
    key: 'sound',
    id: '4975420110286778328',
    pkg: 'works.squirrel.sound',
    name: 'SquirrelSound',
    privacy:
      'https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-sound.html',
    iarcCategoryHint: /Utility|Tools|Productivity|Music|Audio|Application/i,
  },
  qr: {
    key: 'qr',
    id: '4972150905351365265',
    pkg: 'works.squirrel.qr',
    name: 'SquirrelQR',
    privacy:
      'https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-qr.html',
    iarcCategoryHint: /Utility|Tools|Productivity|Application/i,
  },
};

/** Default card order for a full pass (one app at a time). */
export const DEFAULT_CARDS = [
  'privacy',
  'signin',
  'ads',
  'adid',
  'iarc',
  'audience',
  'datasafety',
  'government',
  'financial',
  'health',
  'testers',
];

export function appContentBase(devId = DEV_ID) {
  return `https://play.google.com/console/u/0/developers/${devId}/app`;
}

/** Manual click hints when automation cannot complete a card. */
export const MANUAL = {
  privacy: (app) =>
    `MANUAL: App content → Privacy policy → Manage → paste ${app.privacy} → Save`,
  signin:
    'MANUAL: App content → App access / Sign in details → “All functionality is available without special access” → Save',
  ads: 'MANUAL: App content → Ads → No (does not contain ads) → Save',
  adid:
    'MANUAL: App content → Advertising ID → No / does not use advertising ID → Save',
  iarc:
    `MANUAL: App content → Content ratings → Start questionnaire → email ${CONTACT_EMAIL} → category Utility/Tools → answer No to violence/sexual/UGC/gambling/drugs → Calculate → Submit`,
  audience:
    'MANUAL: App content → Target audience → select 18 and over only → not Designed for Families / not primarily for children → Save',
  datasafety:
    'MANUAL: App content → Data safety → confirm No data collected/shared → Submit',
  government:
    'MANUAL: App content → Government apps → No → Save',
  financial:
    'MANUAL: App content → Financial features → “does not provide any financial features” / None → Save',
  health:
    'MANUAL: App content → Health → “My app does not have any health features” → Save',
  testers: `MANUAL: Test and release → Testing → Internal testing → Testers → create/select email list including ${CONTACT_EMAIL} → Save`,
};
