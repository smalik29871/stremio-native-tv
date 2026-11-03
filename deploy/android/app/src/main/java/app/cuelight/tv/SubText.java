package app.cuelight.tv;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/* The tags of a subtitle line that must never be drawn, whichever way the file wrote them: ASS overrides ({\an8},
 * {\i1}, { \an8 }, {\an8 with no end, {an8}), HTML-like tags (<i>, </font>, <font color="#00ffff">), those with
 * look-alike signs (the angle brackets U+2039/203A, U+3008/3009, U+27E8/27E9, U+2329/232A, U+FF1C/FF1E) or escaped
 * (&lt;i&gt;), and ASS drawings. The same rules as src/sub_text.h (the C app's own lines); this one is for the lines
 * ExoPlayer draws (CuelightPlayer). Pure Java, so tests/java/SubTextTest.java runs without Android. */
final class SubText {
    private SubText() { }

    private static final Pattern OVERRIDE = Pattern.compile("[{\\uFF5B]([^{}\\uFF5B\\uFF5D]{0,120})[}\\uFF5D]");
    private static final Pattern NO_END = Pattern.compile("[{\\uFF5B]\\s*\\\\[^\\s{}\\uFF5B\\uFF5D]{0,60}");
    private static final Pattern TAG = Pattern.compile("[<\\u2039\\u3008\\u27E8\\u2329\\uFF1C]\\s?/?[A-Za-z][^<>\\u2039\\u203A\\u3008\\u3009\\u27E8\\u27E9\\u2329\\u232A\\uFF1C\\uFF1E]{0,60}[>\\u203A\\u3009\\u27E9\\u232A\\uFF1E]");
    private static final Pattern ESCAPED_TAG = Pattern.compile("&lt;\\s?/?[A-Za-z][^&<>]{0,60}&gt;");
    private static final Pattern ESCAPED_BRACES = Pattern.compile("&(?:#123|#x7[Bb]|lbrace|lcub);([^&]{0,120})&(?:#125|#x7[Dd]|rbrace|rcub);");
    private static final Pattern AN = Pattern.compile("(?i)\\s*an[1-9]\\s*");

    /* the text between braces: an override tag rather than a word ("{sighs}" stays) */
    private static boolean isOverride(String inner) {
        return inner.indexOf('\\') >= 0 || AN.matcher(inner).matches();
    }

    /* [start, end) of every stretch to delete, in order and not overlapping */
    static List<int[]> ranges(String s) {
        List<int[]> r = new ArrayList<>();
        Matcher m = OVERRIDE.matcher(s);
        while (m.find()) {
            if (!isOverride(m.group(1))) continue;
            r.add(new int[] { m.start(), m.end() });
        }
        m = ESCAPED_BRACES.matcher(s);
        while (m.find()) if (isOverride(m.group(1))) r.add(new int[] { m.start(), m.end() });
        m = NO_END.matcher(s);
        while (m.find()) r.add(new int[] { m.start(), m.end() });
        m = TAG.matcher(s);
        while (m.find()) r.add(new int[] { m.start(), m.end() });
        m = ESCAPED_TAG.matcher(s);
        while (m.find()) r.add(new int[] { m.start(), m.end() });
        r.sort((a, b) -> a[0] != b[0] ? Integer.compare(a[0], b[0]) : Integer.compare(b[1], a[1]));
        List<int[]> merged = new ArrayList<>();
        for (int[] x : r) {
            if (!merged.isEmpty() && x[0] <= merged.get(merged.size() - 1)[1]) {
                int[] last = merged.get(merged.size() - 1);
                if (x[1] > last[1]) last[1] = x[1];
            } else merged.add(new int[] { x[0], x[1] });
        }
        return merged;
    }

    /* the text with those stretches gone, and the edges trimmed (for tests, and where the text has no spans) */
    static String clean(String s) {
        List<int[]> r = ranges(s);
        if (r.isEmpty()) return s;
        StringBuilder b = new StringBuilder(s);
        for (int k = r.size() - 1; k >= 0; k--) b.delete(r.get(k)[0], r.get(k)[1]);
        return b.toString().trim();
    }
}
