package app.cuelight.tv;

/* Runs without Android: javac -d out deploy/.../SubText.java tests/java/SubTextTest.java && java -cp out app.cuelight.tv.SubTextTest */
public class SubTextTest {
    private static int failures;

    private static void is(String in, String want) {
        String got = SubText.clean(in);
        if (!got.equals(want)) { failures++; System.err.println("FAIL: \"" + in + "\" -> \"" + got + "\", wanted \"" + want + "\""); }
    }

    public static void main(String[] a) {
        is("〈font color=\"#00ffff\"〉:::::[GWC]:::::〈/font〉", ":::::[GWC]:::::");   // the one on the photo
        is("<font color=\"#00ffff\">:::::[GWC]:::::</font>", ":::::[GWC]:::::");
        is("&lt;font color=\"#00ffff\"&gt;hi&lt;/font&gt; there", "hi there");
        is("＜i＞x＜/i＞", "x");
        is("‹b›bold‹/b›", "bold");
        is("{\\an8}On August 14", "On August 14");
        is("{\\an8} On August 14", "On August 14");
        is("{ \\an8 }Spaces inside", "Spaces inside");
        is("{\\\\an8}Doubled backslash", "Doubled backslash");
        is("{\\an8 On August, no end", "On August, no end");
        is("{an8}Lost backslash", "Lost backslash");
        is("&#123;\\an8&#125;Entities", "Entities");
        is("｛\\an8｝Fullwidth", "Fullwidth");
        is("{\\i1}Italic{\\i0} plain", "Italic plain");
        is("{\\pos(192,10)}Over a sign", "Over a sign");
        is("{\\an8}<i>Both kinds</i>", "Both kinds");
        is("{sighs} Fine.", "{sighs} Fine.");           // words in braces stay
        is("I <3 you", "I <3 you");                     // a lone < stays
        is("I ‹3 you", "I ‹3 you");
        is("1 < 2 and 3 > 2", "1 < 2 and 3 > 2");
        is("&lt;3 and &lt;&gt;", "&lt;3 and &lt;&gt;");
        is("  {\\an8}  Padded  ", "Padded");
        is("Plain line", "Plain line");
        is("", "");
        if (failures > 0) { System.err.println(failures + " failure(s)"); System.exit(1); }
        System.out.println("SubText: ok");
    }
}
