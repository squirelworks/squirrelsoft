package works.squirrel.qr;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/*
 * Host smoke test for the Android JNI bridge (app/src/main/cpp/sqqr_jni.c).
 * It declares the same natives as SqQr.kt (same class name, so the same JNI
 * symbols), loads libsqqr_jni built for this machine, and checks every entry
 * point: make a code, decode its picture, read Wi-Fi fields, build a WIFI:
 * payload, and the error paths. Run by `make test-jni` in squirrelqr.
 * All payloads are made up.
 */
public final class SqQr {
    static native byte[] nativeDecode(byte[] gray, int width, int height, int stride, double[] meta);
    static native byte[] nativeEncode(byte[] data, int level, int[] info);
    static native byte[] nativeContent(byte[] utf8);
    static native byte[] nativeWifi(byte[] ssid, byte[] password, byte[] security, boolean hidden);

    static int checks, failures;

    static void check(boolean ok, String what) {
        checks++;
        if (!ok) {
            failures++;
            System.err.println("FAIL: " + what);
        }
    }

    static byte[] utf8(String s) {
        return s.getBytes(StandardCharsets.UTF_8);
    }

    static List<String> split(byte[] raw) {
        List<String> parts = new ArrayList<>();
        int start = 0;
        for (int i = 0; i < raw.length; i++) {
            if (raw[i] == 0) {
                parts.add(new String(raw, start, i - start, StandardCharsets.UTF_8));
                start = i + 1;
            }
        }
        return parts;
    }

    /* make text at a level, draw it 3 px per module with a 4 module border, decode it back */
    static void roundTrip(String text, int level) {
        int[] info = new int[5];
        byte[] grid = nativeEncode(utf8(text), level, info);
        check(grid != null && info[0] == 0, "encode " + text);
        if (grid == null) return;
        int n = info[2], scale = 3, side = (n + 8) * scale;
        check(n == 17 + 4 * info[1] && grid.length == n * n, "size matches version for " + text);
        check(info[3] >= 0 && info[3] < 8, "mask in range");
        byte[] gray = new byte[side * side];
        for (int y = 0; y < side; y++)
            for (int x = 0; x < side; x++) {
                int r = y / scale - 4, c = x / scale - 4;
                boolean dark = r >= 0 && c >= 0 && r < n && c < n && grid[r * n + c] != 0;
                gray[y * side + x] = (byte) (dark ? 0 : 255);
            }
        double[] meta = new double[12];
        byte[] back = nativeDecode(gray, side, side, side, meta);
        check(back != null && new String(back, StandardCharsets.UTF_8).equals(text), "decode back " + text);
        check((int) meta[10] == info[1], "decoded version " + text);
    }

    public static void main(String[] args) {
        System.load(args[0]);

        roundTrip("https://squirrel.works/qr", 1);
        roundTrip("01234567890123456789", 0);
        roundTrip("スクワレル 🐿 acorns", 3);
        String wifiText = "WIFI:T:WPA;S:Nest;P:acorns;;";
        roundTrip(wifiText, 2);

        /* levels and failures */
        int[] info = new int[5];
        check(nativeEncode(utf8("x"), 4, info) == null, "level 4 refused");
        check(nativeEncode(utf8("x"), 0, new int[2]) == null, "short info refused");
        byte[] huge = new byte[3000];
        java.util.Arrays.fill(huge, (byte) 'x');
        check(nativeEncode(huge, 3, info) == null && info[0] == -6, "too long gives SQQR_ERR_OVERFLOW");
        check(nativeDecode(new byte[10], 64, 64, 64, new double[12]) == null, "short image refused");

        /* content */
        List<String> wifi = split(nativeContent(utf8(wifiText)));
        check(wifi.size() >= 2 && wifi.get(0).equals("wifi") && wifi.get(1).equals("Wi-Fi network"), "wifi type");
        check(wifi.size() % 2 == 0, "labels come with values");
        check(wifi.indexOf("Network") > 0 && wifi.get(wifi.indexOf("Network") + 1).equals("Nest"), "wifi network");
        check(wifi.indexOf("Password") > 0 && wifi.get(wifi.indexOf("Password") + 1).equals("acorns"), "wifi password");
        List<String> plain = split(nativeContent(utf8("just words")));
        check(plain.size() == 2 && plain.get(0).equals("text"), "plain text has no fields");
        List<String> emoji = split(nativeContent(utf8("MECARD:N:Squirrel 🐿;TEL:+81-3-0000-0000;;")));
        check(emoji.get(0).equals("contact") && emoji.contains("Squirrel 🐿"), "4-byte UTF-8 survives");
        List<String> empty = split(nativeContent(new byte[0]));
        check(empty.size() == 2 && empty.get(0).equals("text"), "empty is text");

        /* Wi-Fi builder: escapes and the security default */
        String built = new String(nativeWifi(utf8("Oak;Tree"), utf8("a:b"), utf8(""), true), StandardCharsets.UTF_8);
        String bs = String.valueOf((char) 92);
        check(built.equals("WIFI:T:WPA;S:Oak" + bs + ";Tree;P:a" + bs + ":b;H:true;;"), "wifi built: " + built);
        String open = new String(nativeWifi(utf8("Cafe"), utf8(""), utf8(""), false), StandardCharsets.UTF_8);
        check(open.equals("WIFI:T:nopass;S:Cafe;;"), "open network: " + open);
        List<String> again = split(nativeContent(utf8(built)));
        check(again.get(again.indexOf("Network") + 1).equals("Oak;Tree"), "built payload parses back");

        System.out.println("jni smoke: " + checks + " checks, " + failures + " failed");
        System.exit(failures == 0 ? 0 : 1);
    }
}
