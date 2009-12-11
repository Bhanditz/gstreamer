/* generated by mklangtables.c iso-codes 3.12 */

#include <glib.h>

#define ISO_639_FLAG_2T  (1 << 0)
#define ISO_639_FLAG_2B  (1 << 1)

/* *INDENT-OFF* */

static const struct
{
  const gchar iso_639_1[3];
  const gchar iso_639_2[4];
  guint8 flags;
  guint16 name_offset;
} iso_639_codes[] = {
    /* Afar */
  { "aa", "aar", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 0 },
    /* Abkhazian */
  { "ab", "abk", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 5 },
    /* Avestan */
  { "ae", "ave", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 15 },
    /* Afrikaans */
  { "af", "afr", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 23 },
    /* Akan */
  { "ak", "aka", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 33 },
    /* Amharic */
  { "am", "amh", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 38 },
    /* Aragonese */
  { "an", "arg", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 46 },
    /* Arabic */
  { "ar", "ara", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 56 },
    /* Assamese */
  { "as", "asm", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 63 },
    /* Avaric */
  { "av", "ava", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 72 },
    /* Aymara */
  { "ay", "aym", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 79 },
    /* Azerbaijani */
  { "az", "aze", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 86 },
    /* Bashkir */
  { "ba", "bak", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 98 },
    /* Belarusian */
  { "be", "bel", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 106 },
    /* Bulgarian */
  { "bg", "bul", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 117 },
    /* Bihari languages */
  { "bh", "bih", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 127 },
    /* Bislama */
  { "bi", "bis", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 144 },
    /* Bambara */
  { "bm", "bam", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 152 },
    /* Bengali */
  { "bn", "ben", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 160 },
    /* Tibetan */
  { "bo", "bod", ISO_639_FLAG_2T, 168 },
  { "bo", "tib", ISO_639_FLAG_2B, 168 },
    /* Breton */
  { "br", "bre", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 176 },
    /* Bosnian */
  { "bs", "bos", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 183 },
    /* Catalan; Valencian */
  { "ca", "cat", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 191 },
    /* Chechen */
  { "ce", "che", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 210 },
    /* Chamorro */
  { "ch", "cha", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 218 },
    /* Corsican */
  { "co", "cos", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 227 },
    /* Cree */
  { "cr", "cre", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 236 },
    /* Czech */
  { "cs", "ces", ISO_639_FLAG_2T, 241 },
  { "cs", "cze", ISO_639_FLAG_2B, 241 },
    /* Church Slavic; Old Slavonic; Church Slavonic; Old Bulgarian; Old Church Slavonic */
  { "cu", "chu", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 247 },
    /* Chuvash */
  { "cv", "chv", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 328 },
    /* Welsh */
  { "cy", "cym", ISO_639_FLAG_2T, 336 },
  { "cy", "wel", ISO_639_FLAG_2B, 336 },
    /* Danish */
  { "da", "dan", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 342 },
    /* German */
  { "de", "deu", ISO_639_FLAG_2T, 349 },
  { "de", "ger", ISO_639_FLAG_2B, 349 },
    /* Divehi; Dhivehi; Maldivian */
  { "dv", "div", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 356 },
    /* Dzongkha */
  { "dz", "dzo", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 383 },
    /* Ewe */
  { "ee", "ewe", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 392 },
    /* Greek, Modern (1453-) */
  { "el", "ell", ISO_639_FLAG_2T, 396 },
  { "el", "gre", ISO_639_FLAG_2B, 396 },
    /* English */
  { "en", "eng", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 418 },
    /* Esperanto */
  { "eo", "epo", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 426 },
    /* Spanish; Castilian */
  { "es", "spa", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 436 },
    /* Estonian */
  { "et", "est", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 455 },
    /* Basque */
  { "eu", "eus", ISO_639_FLAG_2T, 464 },
  { "eu", "baq", ISO_639_FLAG_2B, 464 },
    /* Persian */
  { "fa", "fas", ISO_639_FLAG_2T, 471 },
  { "fa", "per", ISO_639_FLAG_2B, 471 },
    /* Fulah */
  { "ff", "ful", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 479 },
    /* Finnish */
  { "fi", "fin", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 485 },
    /* Fijian */
  { "fj", "fij", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 493 },
    /* Faroese */
  { "fo", "fao", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 500 },
    /* French */
  { "fr", "fra", ISO_639_FLAG_2T, 508 },
  { "fr", "fre", ISO_639_FLAG_2B, 508 },
    /* Western Frisian */
  { "fy", "fry", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 515 },
    /* Irish */
  { "ga", "gle", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 531 },
    /* Gaelic; Scottish Gaelic */
  { "gd", "gla", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 537 },
    /* Galician */
  { "gl", "glg", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 561 },
    /* Guarani */
  { "gn", "grn", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 570 },
    /* Gujarati */
  { "gu", "guj", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 578 },
    /* Manx */
  { "gv", "glv", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 587 },
    /* Hausa */
  { "ha", "hau", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 592 },
    /* Hebrew */
  { "he", "heb", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 598 },
    /* Hindi */
  { "hi", "hin", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 605 },
    /* Hiri Motu */
  { "ho", "hmo", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 611 },
    /* Croatian */
  { "hr", "hrv", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 621 },
    /* Haitian; Haitian Creole */
  { "ht", "hat", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 630 },
    /* Hungarian */
  { "hu", "hun", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 654 },
    /* Armenian */
  { "hy", "hye", ISO_639_FLAG_2T, 664 },
  { "hy", "arm", ISO_639_FLAG_2B, 664 },
    /* Herero */
  { "hz", "her", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 673 },
    /* Interlingua (International Auxiliary Language Association) */
  { "ia", "ina", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 680 },
    /* Indonesian */
  { "id", "ind", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 739 },
    /* Interlingue; Occidental */
  { "ie", "ile", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 750 },
    /* Igbo */
  { "ig", "ibo", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 774 },
    /* Sichuan Yi; Nuosu */
  { "ii", "iii", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 779 },
    /* Inupiaq */
  { "ik", "ipk", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 797 },
    /* Ido */
  { "io", "ido", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 805 },
    /* Icelandic */
  { "is", "isl", ISO_639_FLAG_2T, 809 },
  { "is", "ice", ISO_639_FLAG_2B, 809 },
    /* Italian */
  { "it", "ita", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 819 },
    /* Inuktitut */
  { "iu", "iku", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 827 },
    /* Japanese */
  { "ja", "jpn", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 837 },
    /* Javanese */
  { "jv", "jav", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 846 },
    /* Georgian */
  { "ka", "kat", ISO_639_FLAG_2T, 855 },
  { "ka", "geo", ISO_639_FLAG_2B, 855 },
    /* Kongo */
  { "kg", "kon", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 864 },
    /* Kikuyu; Gikuyu */
  { "ki", "kik", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 870 },
    /* Kuanyama; Kwanyama */
  { "kj", "kua", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 885 },
    /* Kazakh */
  { "kk", "kaz", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 904 },
    /* Kalaallisut; Greenlandic */
  { "kl", "kal", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 911 },
    /* Central Khmer */
  { "km", "khm", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 936 },
    /* Kannada */
  { "kn", "kan", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 950 },
    /* Korean */
  { "ko", "kor", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 958 },
    /* Kanuri */
  { "kr", "kau", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 965 },
    /* Kashmiri */
  { "ks", "kas", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 972 },
    /* Kurdish */
  { "ku", "kur", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 981 },
    /* Komi */
  { "kv", "kom", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 989 },
    /* Cornish */
  { "kw", "cor", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 994 },
    /* Kirghiz; Kyrgyz */
  { "ky", "kir", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1002 },
    /* Latin */
  { "la", "lat", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1018 },
    /* Luxembourgish; Letzeburgesch */
  { "lb", "ltz", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1024 },
    /* Ganda */
  { "lg", "lug", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1053 },
    /* Limburgan; Limburger; Limburgish */
  { "li", "lim", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1059 },
    /* Lingala */
  { "ln", "lin", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1092 },
    /* Lao */
  { "lo", "lao", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1100 },
    /* Lithuanian */
  { "lt", "lit", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1104 },
    /* Luba-Katanga */
  { "lu", "lub", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1115 },
    /* Latvian */
  { "lv", "lav", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1128 },
    /* Malagasy */
  { "mg", "mlg", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1136 },
    /* Marshallese */
  { "mh", "mah", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1145 },
    /* Maori */
  { "mi", "mri", ISO_639_FLAG_2T, 1157 },
  { "mi", "mao", ISO_639_FLAG_2B, 1157 },
    /* Macedonian */
  { "mk", "mkd", ISO_639_FLAG_2T, 1163 },
  { "mk", "mac", ISO_639_FLAG_2B, 1163 },
    /* Malayalam */
  { "ml", "mal", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1174 },
    /* Mongolian */
  { "mn", "mon", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1184 },
    /* Moldavian; Moldovan */
  { "mo", "mol", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1194 },
    /* Marathi */
  { "mr", "mar", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1214 },
    /* Malay */
  { "ms", "msa", ISO_639_FLAG_2T, 1222 },
  { "ms", "may", ISO_639_FLAG_2B, 1222 },
    /* Maltese */
  { "mt", "mlt", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1228 },
    /* Burmese */
  { "my", "mya", ISO_639_FLAG_2T, 1236 },
  { "my", "bur", ISO_639_FLAG_2B, 1236 },
    /* Nauru */
  { "na", "nau", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1244 },
    /* Bokm?l, Norwegian; Norwegian Bokm?l */
  { "nb", "nob", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1250 },
    /* Ndebele, North; North Ndebele */
  { "nd", "nde", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1288 },
    /* Nepali */
  { "ne", "nep", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1318 },
    /* Ndonga */
  { "ng", "ndo", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1325 },
    /* Dutch; Flemish */
  { "nl", "nld", ISO_639_FLAG_2T, 1332 },
  { "nl", "dut", ISO_639_FLAG_2B, 1332 },
    /* Norwegian Nynorsk; Nynorsk, Norwegian */
  { "nn", "nno", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1347 },
    /* Norwegian */
  { "no", "nor", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1385 },
    /* Ndebele, South; South Ndebele */
  { "nr", "nbl", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1395 },
    /* Navajo; Navaho */
  { "nv", "nav", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1425 },
    /* Chichewa; Chewa; Nyanja */
  { "ny", "nya", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1440 },
    /* Occitan (post 1500) */
  { "oc", "oci", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1464 },
    /* Ojibwa */
  { "oj", "oji", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1484 },
    /* Oromo */
  { "om", "orm", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1491 },
    /* Oriya */
  { "or", "ori", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1497 },
    /* Ossetian; Ossetic */
  { "os", "oss", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1503 },
    /* Panjabi; Punjabi */
  { "pa", "pan", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1521 },
    /* Pali */
  { "pi", "pli", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1538 },
    /* Polish */
  { "pl", "pol", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1543 },
    /* Pushto; Pashto */
  { "ps", "pus", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1550 },
    /* Portuguese */
  { "pt", "por", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1565 },
    /* Quechua */
  { "qu", "que", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1576 },
    /* Romansh */
  { "rm", "roh", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1584 },
    /* Rundi */
  { "rn", "run", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1592 },
    /* Romanian */
  { "ro", "ron", ISO_639_FLAG_2T, 1598 },
  { "ro", "rum", ISO_639_FLAG_2B, 1598 },
    /* Russian */
  { "ru", "rus", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1607 },
    /* Kinyarwanda */
  { "rw", "kin", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1615 },
    /* Sanskrit */
  { "sa", "san", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1627 },
    /* Sardinian */
  { "sc", "srd", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1636 },
    /* Sindhi */
  { "sd", "snd", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1646 },
    /* Northern Sami */
  { "se", "sme", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1653 },
    /* Sango */
  { "sg", "sag", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1667 },
    /* Sinhala; Sinhalese */
  { "si", "sin", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1673 },
    /* Slovak */
  { "sk", "slk", ISO_639_FLAG_2T, 1692 },
  { "sk", "slo", ISO_639_FLAG_2B, 1692 },
    /* Slovenian */
  { "sl", "slv", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1699 },
    /* Samoan */
  { "sm", "smo", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1709 },
    /* Shona */
  { "sn", "sna", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1716 },
    /* Somali */
  { "so", "som", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1722 },
    /* Albanian */
  { "sq", "sqi", ISO_639_FLAG_2T, 1729 },
  { "sq", "alb", ISO_639_FLAG_2B, 1729 },
    /* Serbian */
  { "sr", "srp", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1738 },
    /* Swati */
  { "ss", "ssw", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1746 },
    /* Sotho, Southern */
  { "st", "sot", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1752 },
    /* Sundanese */
  { "su", "sun", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1768 },
    /* Swedish */
  { "sv", "swe", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1778 },
    /* Swahili */
  { "sw", "swa", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1786 },
    /* Tamil */
  { "ta", "tam", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1794 },
    /* Telugu */
  { "te", "tel", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1800 },
    /* Tajik */
  { "tg", "tgk", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1807 },
    /* Thai */
  { "th", "tha", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1813 },
    /* Tigrinya */
  { "ti", "tir", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1818 },
    /* Turkmen */
  { "tk", "tuk", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1827 },
    /* Tagalog */
  { "tl", "tgl", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1835 },
    /* Tswana */
  { "tn", "tsn", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1843 },
    /* Tonga (Tonga Islands) */
  { "to", "ton", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1850 },
    /* Turkish */
  { "tr", "tur", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1872 },
    /* Tsonga */
  { "ts", "tso", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1880 },
    /* Tatar */
  { "tt", "tat", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1887 },
    /* Twi */
  { "tw", "twi", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1893 },
    /* Tahitian */
  { "ty", "tah", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1897 },
    /* Uighur; Uyghur */
  { "ug", "uig", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1906 },
    /* Ukrainian */
  { "uk", "ukr", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1921 },
    /* Urdu */
  { "ur", "urd", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1931 },
    /* Uzbek */
  { "uz", "uzb", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1936 },
    /* Venda */
  { "ve", "ven", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1942 },
    /* Vietnamese */
  { "vi", "vie", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1948 },
    /* Volap?k */
  { "vo", "vol", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1959 },
    /* Walloon */
  { "wa", "wln", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1968 },
    /* Wolof */
  { "wo", "wol", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1976 },
    /* Xhosa */
  { "xh", "xho", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1982 },
    /* Yiddish */
  { "yi", "yid", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1988 },
    /* Yoruba */
  { "yo", "yor", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 1996 },
    /* Zhuang; Chuang */
  { "za", "zha", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 2003 },
    /* Chinese */
  { "zh", "zho", ISO_639_FLAG_2T, 2018 },
  { "zh", "chi", ISO_639_FLAG_2B, 2018 },
    /* Zulu */
  { "zu", "zul", ISO_639_FLAG_2T | ISO_639_FLAG_2B, 2026 },
};

const gchar iso_639_names[] =
  "Afar\000Abkhazian\000Avestan\000Afrikaans\000Akan\000Amharic\000Aragonese"
  "\000Arabic\000Assamese\000Avaric\000Aymara\000Azerbaijani\000Bashkir\000B"
  "elarusian\000Bulgarian\000Bihari languages\000Bislama\000Bambara\000Benga"
  "li\000Tibetan\000Breton\000Bosnian\000Catalan; Valencian\000Chechen\000Ch"
  "amorro\000Corsican\000Cree\000Czech\000Church Slavic; Old Slavonic; Churc"
  "h Slavonic; Old Bulgarian; Old Church Slavonic\000Chuvash\000Welsh\000Dan"
  "ish\000German\000Divehi; Dhivehi; Maldivian\000Dzongkha\000Ewe\000Greek, "
  "Modern (1453-)\000English\000Esperanto\000Spanish; Castilian\000Estonian"
  "\000Basque\000Persian\000Fulah\000Finnish\000Fijian\000Faroese\000French"
  "\000Western Frisian\000Irish\000Gaelic; Scottish Gaelic\000Galician\000Gu"
  "arani\000Gujarati\000Manx\000Hausa\000Hebrew\000Hindi\000Hiri Motu\000Cro"
  "atian\000Haitian; Haitian Creole\000Hungarian\000Armenian\000Herero\000In"
  "terlingua (International Auxiliary Language Association)\000Indonesian"
  "\000Interlingue; Occidental\000Igbo\000Sichuan Yi; Nuosu\000Inupiaq\000Id"
  "o\000Icelandic\000Italian\000Inuktitut\000Japanese\000Javanese\000Georgia"
  "n\000Kongo\000Kikuyu; Gikuyu\000Kuanyama; Kwanyama\000Kazakh\000Kalaallis"
  "ut; Greenlandic\000Central Khmer\000Kannada\000Korean\000Kanuri\000Kashmi"
  "ri\000Kurdish\000Komi\000Cornish\000Kirghiz; Kyrgyz\000Latin\000Luxembour"
  "gish; Letzeburgesch\000Ganda\000Limburgan; Limburger; Limburgish\000Linga"
  "la\000Lao\000Lithuanian\000Luba-Katanga\000Latvian\000Malagasy\000Marshal"
  "lese\000Maori\000Macedonian\000Malayalam\000Mongolian\000Moldavian; Moldo"
  "van\000Marathi\000Malay\000Maltese\000Burmese\000Nauru\000Bokm\303\245l, "
  "Norwegian; Norwegian Bokm\303\245l\000Ndebele, North; North Ndebele\000Ne"
  "pali\000Ndonga\000Dutch; Flemish\000Norwegian Nynorsk; Nynorsk, Norwegian"
  "\000Norwegian\000Ndebele, South; South Ndebele\000Navajo; Navaho\000Chich"
  "ewa; Chewa; Nyanja\000Occitan (post 1500)\000Ojibwa\000Oromo\000Oriya\000"
  "Ossetian; Ossetic\000Panjabi; Punjabi\000Pali\000Polish\000Pushto; Pashto"
  "\000Portuguese\000Quechua\000Romansh\000Rundi\000Romanian\000Russian\000K"
  "inyarwanda\000Sanskrit\000Sardinian\000Sindhi\000Northern Sami\000Sango"
  "\000Sinhala; Sinhalese\000Slovak\000Slovenian\000Samoan\000Shona\000Somal"
  "i\000Albanian\000Serbian\000Swati\000Sotho, Southern\000Sundanese\000Swed"
  "ish\000Swahili\000Tamil\000Telugu\000Tajik\000Thai\000Tigrinya\000Turkmen"
  "\000Tagalog\000Tswana\000Tonga (Tonga Islands)\000Turkish\000Tsonga\000Ta"
  "tar\000Twi\000Tahitian\000Uighur; Uyghur\000Ukrainian\000Urdu\000Uzbek"
  "\000Venda\000Vietnamese\000Volap\303\274k\000Walloon\000Wolof\000Xhosa"
  "\000Yiddish\000Yoruba\000Zhuang; Chuang\000Chinese\000Zulu";

/* *INDENT-ON* */
