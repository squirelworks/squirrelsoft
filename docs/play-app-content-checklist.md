# Play Console — App content checklist (Suica / Sound / QR)

Exact answers for **Policy and programs → App content** for:

| Package | App |
|---------|-----|
| `works.squirrel.suica` | Suica Reader |
| `works.squirrel.sound` | SquirrelSound |
| `works.squirrel.qr` | SquirrelQR |

**API note:** For ordinary Play-hosted apps, Android Publisher v3 can write **Data Safety** (`applications.dataSafety`) and store listing contact fields. It **cannot** set privacy policy URL, ads, content rating (IARC), target audience, government, financial features, health, or sign-in / app access. The App Store Review `policyDeclarations` API is only for **third-party app stores** (`403` if called with `com.android.vending`). Complete the items below in Play Console (human account).

Contact email used elsewhere: `squirelworks@gmail.com`.

---

## 1. Privacy policy — paste URL, Save

Path: **App content → Privacy policy → Start / Manage**

| App | URL (must return HTTP 200) |
|-----|----------------------------|
| Suica | https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-suica.html |
| Sound | https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-sound.html |
| QR | https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-qr.html |

Use `@main` only (not a feature-branch tip).

---

## 2. Sign in details (App access)

Path: **App content → App access** (sometimes labeled Sign-in / testing credentials)

- **All functionality is available without special access** / no part of the app is restricted.
- App does **not** require account sign-in, membership, payments, referral codes, or other credentials.
- Do **not** add test usernames/passwords.

Same answer for all three apps.

---

## 3. Ads

Path: **App content → Ads → Start / Manage**

- **No**, this app does not contain ads.

If a separate **Advertising ID** card appears:

- App does **not** use advertising ID (no ad SDKs).

Same for all three.

---

## 4. Content rating (IARC)

Path: **App content → Content ratings → Start questionnaire**

- Email: `squirelworks@gmail.com`
- Category: **Utility / Productivity / Tools** (Sound: audio utility / music & audio tool if listed)
- Violence, sexual content, language, controlled substances, gambling, user-generated content sharing: **No**
- In-app purchases / cash trading / social features / location sharing: **No**
- Sensor notes (for honesty if asked): Suica uses **NFC** (on-device card read); Sound uses **microphone** (on-device spectrum); QR uses **camera** (on-device decode). No upload of that sensor data by the app.
- Submit calculated ratings for each app.

---

## 5. Target audience and content

Path: **App content → Target audience and content**

Recommended (all three):

- Target age groups: **18 and over** only  
  (or consecutive **13–15 + 16–17 + 18+** if you prefer 13+; do **not** include under-13)
- **Not** Designed for Families / not primarily appealing to children
- Optional “restrict minors” (shown for 18+ only): leave unchecked unless you intentionally want Play to block minors

---

## 6. Data safety

Path: **App content → Data safety**

Already written via API (`scripts/play_data/data_safety_no_data.csv`):

- App does **not** collect or share any of the listed user data types (on-device only).
- No account creation.

In Console: open Data safety and **Submit / confirm** if it still shows as draft after the API import.

---

## 7. Government apps

Path: **App content → Government apps**

- **No** — not developed by or on behalf of a government.

---

## 8. Financial features

Path: **App content → Financial features**

- **The app does not provide any financial features** / select **None**.

Rationale (Suica): the app is a **read-only NFC reader** that displays on-device FeliCa transit/e-money balance and history. It does **not** move money, process payments, offer wallets, banking, loans, or crypto. Sound and QR have no financial features.

---

## 9. Health

Path: **App content → Health**

- **The app does not have any health features**.

---

## Store category (related, not always under App content)

Path: **Store presence → Store settings → App category**

| App | Category |
|-----|----------|
| Suica / QR | Tools |
| Sound | Music & Audio |

---

## Quick copy matrix

| Item | Answer (all three unless noted) |
|------|----------------------------------|
| Privacy policy | jsDelivr `@main` URL per app (table above) |
| Sign-in / App access | No restricted parts; no accounts |
| Ads | No |
| Advertising ID | Not used |
| Content rating | Tools/utility; all sensitive content = No |
| Target audience | 18+ (or 13+ consecutive bands); not for children |
| Data safety | No data collected/shared (API done; confirm Submit) |
| Government | No |
| Financial features | None (Suica = on-device card display only) |
| Health | None |
