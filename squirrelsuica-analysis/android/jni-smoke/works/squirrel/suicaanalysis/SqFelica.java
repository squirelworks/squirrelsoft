package works.squirrel.suicaanalysis;

/*
 * Host smoke test for the Android JNI bridge (app/src/main/cpp/sqfelica_jni.c).
 * It declares the same natives as SqFelica.kt (same class name, so the same JNI
 * symbols), loads libsqfelica_jni built for this machine, and checks history
 * and balance over synthetic demo blocks from libs/sqfelica's parity fixture
 * (gen_fixture.py / HistoryTextTest). Run by `make test-jni` in
 * squirrelsuica-analysis. All payloads are made up — never real card dumps.
 */
public final class SqFelica {
    static final int BRAND_SUICA = 0, BRAND_NANACO = 1, BRAND_EDY = 2, BRAND_WAON = 3;
    static final int FIELDS = 18;

    static native int[] nativeHistory(int brand, byte[] blocks);
    static native int nativeBalance(int brand, byte[] a, byte[] b);

    static int checks, failures;

    static void check(boolean ok, String what) {
        checks++;
        if (!ok) {
            failures++;
            System.err.println("FAIL: " + what);
        }
    }

    static byte[] hex(String h) {
        int n = h.length() / 2;
        byte[] out = new byte[n];
        for (int i = 0; i < n; i++)
            out[i] = (byte) Integer.parseInt(h.substring(i * 2, i * 2 + 2), 16);
        return out;
    }

    static byte[] concat(String... blocks) {
        byte[] out = new byte[blocks.length * 16];
        for (int i = 0; i < blocks.length; i++)
            System.arraycopy(hex(blocks[i]), 0, out, i * 16, 16);
        return out;
    }

    static int at(int[] v, int row, int field) {
        return v[row * FIELDS + field];
    }

    public static void main(String[] args) {
        System.load(args[0]);

        /* Suica demo group from gen_fixture.py (parity.txt first G suica). */
        byte[] suica = concat(
                "16010000355ce359e35eb00900000000",
                "16010000355b2507250a580a00000000",
                "16020000355a010100002a0b00000000",
                "160100003559250125025a0300000000");
        int[] hist = nativeHistory(BRAND_SUICA, suica);
        check(hist != null && hist.length == 4 * FIELDS, "suica history length: " + (hist == null ? -1 : hist.length));
        if (hist != null && hist.length >= FIELDS) {
            check(at(hist, 0, 0) == 0, "suica[0] kind train");
            check(at(hist, 0, 1) == 2026 && at(hist, 0, 2) == 10 && at(hist, 0, 3) == 28, "suica[0] date");
            check(at(hist, 0, 4) == -1 && at(hist, 0, 5) == -1, "suica[0] no time");
            check(at(hist, 0, 6) == 168 && at(hist, 0, 7) == 2480, "suica[0] fare/balance");
            check(at(hist, 0, 9) == 0x16 && at(hist, 0, 10) == 0x01, "suica[0] terminal/process");
            check(at(hist, 0, 11) == 1 && at(hist, 0, 12) == 0xE3 && at(hist, 0, 13) == 0x59, "suica[0] enter");
            check(at(hist, 0, 14) == 1 && at(hist, 0, 15) == 0xE3 && at(hist, 0, 16) == 0x5E, "suica[0] exit");
            check(at(hist, 2, 0) == 3 && at(hist, 2, 6) == 2000 && at(hist, 2, 7) == 2858, "suica charge row");
        }

        /* Nanaco / Edy synthetic blocks from gen_fixture.py. */
        byte[] nanaco = concat(
                "47000001e0000014500355c4aa000000",
                "6f00000bb8000016300355b305000000");
        hist = nativeHistory(BRAND_NANACO, nanaco);
        check(hist != null && hist.length == 2 * FIELDS, "nanaco history length");
        if (hist != null && hist.length >= FIELDS) {
            check(at(hist, 0, 0) == 2 && at(hist, 0, 6) == 480 && at(hist, 0, 7) == 5200, "nanaco sale");
            check(at(hist, 0, 1) == 2026 && at(hist, 0, 4) == 18 && at(hist, 0, 5) == 42, "nanaco time");
            check(at(hist, 1, 0) == 3 && at(hist, 1, 7) == 5680, "nanaco charge");
        }

        byte[] edy = concat(
                "200000004c9106ff000001e000001450",
                "020000004c8e0e1000000bb800001630");
        hist = nativeHistory(BRAND_EDY, edy);
        check(hist != null && hist.length == 2 * FIELDS, "edy history length");
        if (hist != null && hist.length >= FIELDS) {
            check(at(hist, 0, 0) == 2 && at(hist, 0, 6) == 480 && at(hist, 0, 7) == 5200, "edy sale");
            check(at(hist, 0, 4) == 18 && at(hist, 0, 5) == 42, "edy time");
            check(at(hist, 1, 0) == 3 && at(hist, 1, 7) == 5680, "edy charge");
        }

        /* WAON 0x680b: synthetic 32-byte record (sale ¥620, bal ¥4584, 2026-10-28 19:10). */
        byte[] waon = concat(
                "00000000000000000000000000000000",
                "0004ad7265023d001360000000000000");
        hist = nativeHistory(BRAND_WAON, waon);
        check(hist != null && hist.length == FIELDS, "waon history length");
        if (hist != null && hist.length >= FIELDS) {
            check(at(hist, 0, 0) == 2 && at(hist, 0, 6) == 620 && at(hist, 0, 7) == 4584, "waon sale");
            check(at(hist, 0, 1) == 2026 && at(hist, 0, 2) == 10 && at(hist, 0, 3) == 28, "waon date");
            check(at(hist, 0, 4) == 19 && at(hist, 0, 5) == 10, "waon time");
        }
        hist = nativeHistory(BRAND_WAON, new byte[32]);
        check(hist != null && hist.length == 0, "waon zero record skipped");
        hist = nativeHistory(99, suica);
        check(hist != null && hist.length == 0, "unknown brand history empty");
        hist = nativeHistory(BRAND_SUICA, null);
        check(hist != null && hist.length == 0, "null blocks empty");
        hist = nativeHistory(BRAND_SUICA, new byte[0]);
        check(hist != null && hist.length == 0, "empty blocks empty");
        hist = nativeHistory(BRAND_SUICA, new byte[16]); /* all-zero skipped */
        check(hist != null && hist.length == 0, "zero block skipped");

        /* Balances from the parity L lines (synthetic). */
        check(nativeBalance(BRAND_SUICA, hex("16010000342100000000b00900000000"), null) == 2480,
                "suica balance from 090f");
        check(nativeBalance(BRAND_SUICA, null, hex("0000000000000000000000b009000000")) == 2480,
                "suica balance from 008b");
        check(nativeBalance(BRAND_SUICA,
                hex("16010000342100000000307500000000"),
                hex("0000000000000000000000e803000000")) == 1000,
                "suica prefers 008b when 090f is out of range");
        check(nativeBalance(BRAND_NANACO, hex("50140000000000000000000000000000"), null) == 5200,
                "nanaco balance");
        check(nativeBalance(BRAND_EDY, hex("50140000000000000000000000000000"), null) == 5200,
                "edy balance");
        check(nativeBalance(BRAND_WAON, hex("50140000000000000000000000000000"), null) == 5200,
                "waon balance");
        check(nativeBalance(BRAND_SUICA, null, null) == -1, "missing suica blocks");
        check(nativeBalance(99, hex("50140000000000000000000000000000"), null) == -1,
                "unknown brand balance");

        System.out.println("jni smoke: " + checks + " checks, " + failures + " failed");
        System.exit(failures == 0 ? 0 : 1);
    }
}
