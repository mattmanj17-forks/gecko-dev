/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// List of standard font families installed as part of Windows 10
// from https://docs.microsoft.com/en-us/typography/fonts/windows_10_font_list
// TODO: check whether we need to list legacy styled family names like "... Light".
static const char* kBaseFonts[] = {
  "Arial",
  "Arial Black",
  "Bahnschrift",
  "Bahnschrift Light",
  "Bahnschrift SemiBold",
  "Bahnschrift SemiLight",
  "Calibri",
  "Calibri Light",
  "Cambria",
  "Cambria Math",
  "Candara",
  "Comic Sans MS",
  "Consolas",
  "Constantia",
  "Corbel",
  "Courier New",
  "Ebrima",
  "Franklin Gothic Medium",
  "Gabriola",
  "Gadugi",
  "Georgia",
  "Impact",
  "Javanese Text",
  "Leelawadee UI",
  "Leelawadee UI Semilight",
  "Lucida Console",
  "Lucida Sans Unicode",
  "Malgun Gothic",
  "Malgun Gothic Semilight",
  "Marlett",
  "Microsoft Himalaya",
  "Microsoft JhengHei",
  "Microsoft JhengHei Light",
  "Microsoft JhengHei UI",
  "Microsoft JhengHei UI Light",
  "Microsoft New Tai Lue",
  "Microsoft PhagsPa",
  "Microsoft Sans Serif",
  "Microsoft Tai Le",
  "Microsoft YaHei",
  "Microsoft YaHei Light",
  "Microsoft YaHei UI",
  "Microsoft YaHei UI Light",
  "Microsoft Yi Baiti",
  "MingLiU-ExtB",
  "MingLiU_HKSCS-ExtB",
  "Mongolian Baiti",
  "MS Gothic",
  "MS PGothic",
  "MS UI Gothic",
  "MV Boli",
  "Myanmar Text",
  "Nirmala UI",
  "Nirmala UI Semilight",
  "NSimSun",
  "Palatino Linotype",
  "PMingLiU-ExtB",
  "Segoe MDL2 Assets",
  "Segoe Print",
  "Segoe Script",
  "Segoe UI",
  "Segoe UI Black",
  "Segoe UI Emoji",
  "Segoe UI Historic",
  "Segoe UI Light",
  "Segoe UI Semibold",
  "Segoe UI Semilight",
  "Segoe UI Symbol",
  "SimSun",
  "SimSun-ExtB",
  "SimSun-ExtG",
  "Sitka Banner",
  "Sitka Display",
  "Sitka Heading",
  "Sitka Small",
  "Sitka Subheading",
  "Sitka Text",
  "Sylfaen",
  "Symbol",
  "Tahoma",
  "Times New Roman",
  "Trebuchet MS",
  "Verdana",
  "Webdings",
  "Wingdings",
  "Yu Gothic",
  "Yu Gothic Light",
  "Yu Gothic Medium",
  "Yu Gothic UI",
  "Yu Gothic UI Light",
  "Yu Gothic UI Semibold",
  "Yu Gothic UI Semilight",
};

// Additional fonts provided by language-pack installation.
static const char* kLangPackFonts[] = {
  "Aharoni",  // Hebrew Supplemental Fonts
  "Aldhabi",  // Arabic Script Supplemental Fonts
  "Andalus",  // Arabic Script Supplemental Fonts
  "Angsana New",  // Thai Supplemental Fonts
  "AngsanaUPC",  // Thai Supplemental Fonts
  "Aparajita",  // Devanagari Supplemental Fonts
  "Arabic Typesetting",  // Arabic Script Supplemental Fonts
  "Batang",  // Korean Supplemental Fonts
  "BatangChe",  // Korean Supplemental Fonts
  "BIZ UDGothic",  // Japanese Supplemental Fonts
  "BIZ UDMincho",  // Japanese Supplemental Fonts
  "BIZ UDPGothic",  // Japanese Supplemental Fonts
  "BIZ UDPMincho",  // Japanese Supplemental Fonts
  "Browallia New",  // Thai Supplemental Fonts
  "BrowalliaUPC",  // Thai Supplemental Fonts
  "Cordia New",  // Thai Supplemental Fonts
  "CordiaUPC",  // Thai Supplemental Fonts
  "DaunPenh",  // Khmer Supplemental Fonts
  "David",  // Hebrew Supplemental Fonts
  "DengXian",  // Chinese (Simplified) Supplemental Fonts
  "DFKai-SB",  // Chinese (Traditional) Supplemental Fonts
  "DilleniaUPC",  // Thai Supplemental Fonts
  "DokChampa",  // Lao Supplemental Fonts
  "Dotum",  // Korean Supplemental Fonts
  "DotumChe",  // Korean Supplemental Fonts
  "Estrangelo Edessa",  // Syriac Supplemental Fonts
  "EucrosiaUPC",  // Thai Supplemental Fonts
  "Euphemia",  // Canadian Aboriginal Syllabics Supplemental Fonts
  "FangSong",  // Chinese (Simplified) Supplemental Fonts
  "FrankRuehl",  // Hebrew Supplemental Fonts
  "FreesiaUPC",  // Thai Supplemental Fonts
  "Gautami",  // Telugu Supplemental Fonts
  "Gisha",  // Hebrew Supplemental Fonts
  "Gulim",  // Korean Supplemental Fonts
  "GulimChe",  // Korean Supplemental Fonts
  "Gungsuh",  // Korean Supplemental Fonts
  "GungsuhChe",  // Korean Supplemental Fonts
  "IrisUPC",  // Thai Supplemental Fonts
  "Iskoola Pota",  // Sinhala Supplemental Fonts
  "JasmineUPC",  // Thai Supplemental Fonts
  "KaiTi",  // Chinese (Simplified) Supplemental Fonts
  "Kalinga",  // Odia Supplemental Fonts
  "Kartika",  // Malayalam Supplemental Fonts
  "Khmer UI",  // Khmer Supplemental Fonts
  "KodchiangUPC",  // Thai Supplemental Fonts
  "Kokila",  // Devanagari Supplemental Fonts
  "Lao UI",  // Lao Supplemental Fonts
  "Latha",  // Tamil Supplemental Fonts
  "Leelawadee",  // Thai Supplemental Fonts
  "Levenim MT",  // Hebrew Supplemental Fonts
  "LilyUPC",  // Thai Supplemental Fonts
  "Mangal",  // Devanagari Supplemental Fonts
  "Meiryo",  // Japanese Supplemental Fonts
  "Meiryo UI",  // Japanese Supplemental Fonts
  "Microsoft Uighur",  // Arabic Script Supplemental Fonts
  "MingLiU",  // Chinese (Traditional) Supplemental Fonts
  "MingLiU_HKSCS",  // Chinese (Traditional) Supplemental Fonts
  "Miriam",  // Hebrew Supplemental Fonts
  "Miriam Fixed",  // Hebrew Supplemental Fonts
  "MoolBoran",  // Khmer Supplemental Fonts
  "MS Mincho",  // Japanese Supplemental Fonts
  "MS PMincho",  // Japanese Supplemental Fonts
  "Narkisim",  // Hebrew Supplemental Fonts
  "Noto Sans HK",  // Chinese (Traditional, Hong Kong) Supplemental Fonts
  "Noto Sans JP",  // Japanese Supplemental Fonts
  "Noto Sans KR",  // Korean Supplemental Fonts
  "Noto Sans SC",  // Chinese (Simplified) Supplemental Fonts
  "Noto Sans TC",  // Chinese (Traditional) Supplemental Fonts
  "Noto Serif HK",  // Chinese (Traditional, Hong Kong) Supplemental Fonts
  "Noto Serif JP",  // Japanese Supplemental Fonts
  "Noto Serif KR",  // Korean Supplemental Fonts
  "Noto Serif SC",  // Chinese (Simplified) Supplemental Fonts
  "Noto Serif TC",  // Chinese (Traditional) Supplemental Fonts
  "Nyala",  // Ethiopic Supplemental Fonts
  "Plantagenet Cherokee",  // Cherokee Supplemental Fonts
  "PMingLiU",  // Chinese (Traditional) Supplemental Fonts
  "Raavi",  // Gurmukhi Supplemental Fonts
  "Rod",  // Hebrew Supplemental Fonts
  "Sakkal Majalla",  // Arabic Script Supplemental Fonts
  "Sanskrit Text",  // Devanagari Supplemental Fonts
  "Shonar Bangla",  // Bangla Script Supplemental Fonts
  "Shruti",  // Gujarati Supplemental Fonts
  "SimHei",  // Chinese (Simplified) Supplemental Fonts
  "Simplified Arabic",  // Arabic Script Supplemental Fonts
  "Traditional Arabic",  // Arabic Script Supplemental Fonts
  "Tunga",  // Kannada Supplemental Fonts
  "UD Digi Kyokasho N-B",  // Japanese Supplemental Fonts
  "UD Digi Kyokasho N-R",  // Japanese Supplemental Fonts
  "UD Digi Kyokasho NK-B",  // Japanese Supplemental Fonts
  "UD Digi Kyokasho NK-R",  // Japanese Supplemental Fonts
  "UD Digi Kyokasho NP-B",  // Japanese Supplemental Fonts
  "UD Digi Kyokasho NP-R",  // Japanese Supplemental Fonts
  "Urdu Typesetting",  // Arabic Script Supplemental Fonts
  "Utsaah",  // Devanagari Supplemental Fonts
  "Vani",  // Telugu Supplemental Fonts
  "Vijaya",  // Tamil Supplemental Fonts
  "Vrinda",  // Bangla Script Supplemental Fonts
  "Yu Mincho",  // Japanese Supplemental Fonts
// Latin/Greek/Cyrillic scripts are already well-supported by the base fonts,
// so we do not include these even when the LangPack collection is enabled.
//  "Arial Nova",  // Pan-European Supplemental Fonts - EXCLUDED
//  "Georgia Pro",  // Pan-European Supplemental Fonts - EXCLUDED
//  "Gill Sans Nova",  // Pan-European Supplemental Fonts - EXCLUDED
//  "Neue Haas Grotesk Text Pro",  // Pan-European Supplemental Fonts - EXCLUDED
//  "Rockwell Nova",  // Pan-European Supplemental Fonts - EXCLUDED
//  "Verdana Pro",  // Pan-European Supplemental Fonts - EXCLUDED
};

struct FontSubstitute {
  const char *substituteName;
  const char *actualFontName;
};

static const FontSubstitute kFontSubstitutes[] = {
  // Common substitutions
  {"Arabic Transparent", "Arial"},
  {"Arial Baltic", "Arial"},
  {"Arial CE", "Arial"},
  {"Arial CYR", "Arial"},
  {"Arial Greek", "Arial"},
  {"Arial TUR", "Arial"},
  {"Courier New Baltic", "Courier New"},
  {"Courier New CE", "Courier New"},
  {"Courier New CYR", "Courier New"},
  {"Courier New Greek", "Courier New"},
  {"Courier New TUR", "Courier New"},
  {"Helv", "Microsoft Sans Serif"},
  {"Helvetica", "Arial"},
  {"MS Shell Dlg 2", "Tahoma"},
  {"Tahoma Armenian", "Tahoma"},
  {"Times", "Times New Roman"},
  {"Times New Roman Baltic", "Times New Roman"},
  {"Times New Roman CE", "Times New Roman"},
  {"Times New Roman CYR", "Times New Roman"},
  {"Times New Roman Greek", "Times New Roman"},
  {"Times New Roman TUR", "Times New Roman"},
  {"Tms Rmn", "Times New Roman"},
  // Common, except Japanese (which uses MS UI Gothic, instead)
  {"MS Shell Dlg", "Microsoft Sans Serif"},
  // Arabic
  {"Arial (Arabic)", "Arial"},
  {"Courier New (Arabic)", "Courier New"},
  {"Times New Roman (Arabic)", "Times New Roman"},
  // Cyrillic + Greek
  {"Courier", "Courier New"},
  // Greek
  {"Fixedsys Greek", "Fixedsys"},
  {"MS Serif Greek", "Times New Roman"},
  {"MS Sans Serif Greek", "Microsoft Sans Serif"},
  {"Small Fonts Greek", "Arial"},
  {"System Greek", "System"},
  // Hebrew
  {"Arial (Hebrew)", "Arial"},
  {"Courier New (Hebrew)", "Courier New"},
  {"David Transparent", "David"},
  {"Fixed Miriam Transparent", "Miriam Fixed"},
  {"Miriam Transparent", "Miriam"},
  {"Rod Transparent", "Rod"},
  {"Times New Roman (Hebrew)", "Times New Roman"},
  // Japanese
  {"標準明朝", "ＭＳ 明朝"},
  {"標準ゴシック", "ＭＳ ゴシック"},
  {"ゴシック", "ＭＳ ゴシック"},
  {"ｺﾞｼｯｸ", "ＭＳ ゴシック"},
  {"ｸｰﾘｴ", "Courier New"},
  {"ﾀｲﾑｽﾞﾛﾏﾝ", "Times New Roman"},
  {"ﾍﾙﾍﾞﾁｶ", "Arial"},
  // Simplified Chinese
  {"FangSong_GB2312", "FangSong"},
  {"KaiTi_GB2312", "KaiTi"},
};
