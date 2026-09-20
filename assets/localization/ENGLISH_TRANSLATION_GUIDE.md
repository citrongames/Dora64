# English localization: glossary and translation rules

Completed text pass: 2026-09-19. The English catalogs contain all 903 dialogue
records and all 105 other records (32 gadget descriptions, 40 names/labels,
9 item-notification fragments, 13 menu strings and 11 system messages).
Completion includes unused case labels and the 53 portrait-test labels; it
does not imply that every record is reachable during a normal playthrough.
The user checked the most complex scenes in the Windows build and confirmed
that they work correctly. A full English playthrough remains pending.

## Source policy and naming

Translate from the locally extracted Japanese ROM text. The Russian catalog
and its guide provide context, not an intermediate language to translate from.
Keep Japanese source dumps local and ignored by Git. Catalogs contain stable
IDs, translations and existing categories only.

Use the familiar Japanese character identities in English: Doraemon, Nobita,
Shizuka, Gian and Suneo. Bandai Namco's official English marketing for the 2019
game uses these names [S1]. The US adaptation's Noby, Sue, Big G and Sneech
refer to the same characters, but are not this project's character names.
Keep the original Japanese story, including the raccoon-dog joke; do not
import the US adaptation's changed setting or story details.

Gadget names must agree with the existing user-created English book textures.
All 32 assembled wooden labels were inspected. Several names have direct
confirmation in official English materials; others are existing project
choices, explicitly marked below. An English name on a fan wiki alone does
not establish an official translation. No official English script of this N64
game was established during this work.

Sources checked on 2026-09-19:

- **S1 — Bandai Namco, Doraemon Story of Seasons (2019):** character names
  and the term secret gadgets.
  https://en.bandainamcoent.eu/doraemon/doraemon-story-of-seasons
- **S2 — TV Asahi, announcement of the US adaptation (2014):** Anywhere Door,
  Hopter, Air Cannon and Translation Gummy; also explains regional character
  renaming, which is not applied to this port.
  https://tvablog.tv-asahi.co.jp/reading/touch/20201//
- **S3 — JINS, licensed Doraemon collaboration (2023):** Pass Loop, Shrink Ray,
  Sonic Solidifier.
  https://us.jins.com/blogs/library/doraemon
- **S4 — Bandai Namco, Friends of the Great Kingdom:** Instant Wardrobe Cam
  in the official outfit instructions.
  https://en.bandainamcoent.eu/doraemon/doraemon-story-of-seasons-friends-of-the-great-kingdom

## Characters, gender and voice

| Japanese | English | Pronouns / context |
| --- | --- | --- |
| ドラえもん | Doraemon | he/him; caring, clear, practical; a cat-type robot |
| のび太 | Nobita | he/him; hesitant and emotional, but brave when friends need him |
| しずか | Shizuka | she/her; gentle, polite, direct |
| ジャイアン | Gian | he/him; boastful and forceful; colloquial, not needlessly cruel |
| スネ夫 | Suneo | he/him; proud, nervous, sometimes sarcastic |
| コロナ | Corona / Princess Corona | she/her; Sky King's daughter; polite but spirited |
| ウッディ | Woody / Mr. Woody | he/him; old tree spirit; warm, measured speech |
| おとうさま | Father | Corona addressing the Sky King; other speakers may say dad |
| まおう | Demon King | he/him; threatening and grandiose |
| だいちのおう | Earth King | he/him |
| かいようのおう | Sea King | he/him |
| てんくうのおう | Sky King | he/him; Corona's father |
| ポン太 | Ponta | he/him in boss context; territorial forest opponent |
| ポケラ | Pokera | species/creature name; use it unless a scene establishes otherwise |
| ロック | Roc | giant bird, not a rock; use it |
| ビッグサンタ / ビッグ・サンタ | Big Santa | he/him; Santa-like ho-ho-ho manner |
| ミノタウロス | Minotaur | he/him; axe and horns; preserve the cow pun when practical |
| ケンタウロス | Centaur | he/him; formal, proud combatant |
| 回転くん / かいてんくん | Spinner | descriptive project name in the portrait test |
| 人魚くん / にんぎょくん | Merman | descriptive portrait label |
| へんなとり | Strange bird | descriptive portrait label |

The haughty flying boss speaks in a feminine voice in the Japanese text;
do not silently turn that speaker into a male boss. The trapped sea NPC is
explicitly a younger brother, and the three racers are brothers. For unnamed
fairies whose gender is not established, avoid adding gendered pronouns.
The mouse's repeated `チュー` is rendered sparingly as `Squeak!`, preserving
the joke without consuming the whole text box. Do not infer gender from a
translated noun alone.

## World and interface glossary

| Japanese | English |
| --- | --- |
| ようせいかい | Spirit World |
| ようせい | fairy / fairies |
| せいれい | spirit / spirits (the elemental beings powering the stones) |
| にんげんかい | human world |
| だいちのくに | Land of Earth |
| かいようのくに / かいようかい | Land of Sea |
| てんくうのくに / てんくうかい | Land of Sky |
| せいれいせき | Spirit Stone / Spirit Stones |
| だいち / かいよう / てんくうのせいれいせき | Earth / Sea / Sky Spirit Stone |
| せいれいせきのかけら | Spirit Stone piece; piece of a Spirit Stone in prose |
| けっかい | barrier |
| せきかのまほう | petrification spell; explain as being turned to stone where needed |
| かざんじょう | Volcano Castle |
| かいていじょう | Undersea Castle |
| てんくうじょう | Sky Castle |
| うみのはかば | Sea Graveyard |
| まおうじょう | Demon King's Castle |
| だいち / かいよう / てんくうのかぎ | Earth / Sea / Sky Key |
| ひみつどうぐ | gadget / secret gadget |
| ひみつどうぐずかん | Gadget Encyclopedia |
| ゲームファイル | game file |
| いまのタイム / ベストタイム | Your Time / Best Time |
| どらやき | dorayaki |
| すず | bell |

Game-specific world names above are project translations of the Japanese,
not claims of an official English release. Proper titles use Title Case;
ordinary references to a king, a fairy, a piece or a gadget do not.

## Gadgets

`Existing` means the name is preserved from the user's English book texture;
an official primary-source spelling was not independently confirmed in this
pass. This distinction prevents project terminology being misrepresented as
official. Update the text, glossary and book label together if a future source
justifies changing a name.

| Japanese | Canonical English book name | Evidence / compact form |
| --- | --- | --- |
| くうきほう | Air Cannon | S2 |
| しょうげきはピストル | Shock Wave Pistol | Existing |
| こけおどしてなげだん | Danger Bomb | Existing; description explains the flash and noise |
| チャンピオングローブ | Champion Gloves | Existing |
| ジーンマイク | Gene Mic | Existing; `ジーン` describes being emotionally moved |
| グレードアップえき | Upgrade Spray | Existing; description retains the liquid's temporary effect |
| タンマウオッチ | Tanma Watch | Existing; do not alternate with Time Stopper |
| やくよけシール | Safety Charm Sticker | Existing |
| ももたろうじるしのきびだんご | Wild Beast-Taming Pellets | Existing; Momotaro-brand millet dumplings |
| スモールライト | Shrink Ray | S3 |
| とおりぬけフープ | Pass Loop | S3 |
| エラチューブ | Air Tubes | Existing |
| タイムふろしき | Time Kerchief | Existing |
| しんかいクリーム | Deep Sea Cream | Existing |
| テキオーとう | Adapting Ray | Existing; keep this spelling in all project text |
| ジャックまめ | Jack Bean | Existing |
| なんでもそうじゅうき | Anything Controller | Existing |
| タケコプター | Hopter | S2 |
| くもかためガス | Extra Hold Cloud Spray | Existing; **Cloud Spray** in the compact pickup name |
| どこでもドア | Anywhere Door | S2 |
| すいちゅうバギー | Underwater Buggy | Existing; Buggy when addressed in conversation |
| タイムテレビ | Time TV | Existing |
| きせかえカメラ | Instant Wardrobe Cam | S4 |
| キャンピングカプセル | Camping Capsule | Existing |
| ほんやくコンニャク | Translation Gummy | S2 |
| もしもボックス | What-If Phone Booth | Existing |
| おざしきつりぼり | Mobile Fishing Pond | Existing |
| やまびこやま | Echo Mountain | Existing |
| いとなしいとでんわ | Cordless Can Phone | Existing |
| えほんはいりこみぐつ | Picture Book Entry Shoes | Existing |
| ミニドラえもん | Mini-Dora | Existing |
| コエカタマリン | Sonic Solidifier | S3 |

`Extra Hold Cloud Spray` has 22 visible characters; the compact record
`00123DFC` has a strict 21-character allowance. `Cloud Spray` is the explicit
short form for that record. Dialogue and the existing book image retain the
full name. Do not change the width limit to hide the mismatch.

## Text, formatting and runtime limits

- Dialogue: at most 37 visible characters per line, 36 on the final line to
  leave room for the cursor, at most 3 lines, and at most 80 visible characters
  over the entire message. Current English dialogue wraps at 36 or fewer.
- Descriptions also pass through `encodeFragment` with the **80-visible-
  character limit**. The general catalog validator checks their 3x source
  allowance but does not enforce this runtime limit. Descriptions were checked
  separately for 80 total, 3 lines and 36 characters per line.
- System and menu records use `encodeMessage`, so dialogue limits apply there
  too. Keep all supported glyphs in `dialogue_font.json`; never regenerate or
  overwrite the user's hand-drawn atlas.
- Preserve every `{F7}` and `{F8:XX}`. Put colors around the same translated
  concept. F7 resets the color; F8:31 marks items/actions; F8:32 names/objects;
  F8:34 emotional emphasis or the expression in portrait-test labels.
- Preserve the source counts of `「` and `」`, including unusual unmatched
  quotes and opening/closing quotes split across pages. They are visible
  glyphs. Do not add a closing quote to every page of a continuing speech.
- Newlines and formatting commands are not visible characters. The supported
  single-glyph ellipsis `…` represents Japanese pauses. Use straight English
  apostrophes; avoid unsupported typographic punctuation.
- Never break a word or color command. Read adjacent pages together before
  shortening: a sentence may continue in the next entry.

## Composed messages

`func_800778C0` combines the item name, a newline and a suffix in one message.
English suffixes are ` recovered!`, ` obtained!` and ` found!`; the name still
comes first. A second independent message reports the weapon progress or
gadget count. All translated name/suffix pairs were checked for width and
capacity.

The game places digits before `00123FDF`. Use ` in your collection!` so both
`1 in your collection!` and `32 in your collection!` are grammatical. A fixed
`th` suffix would break 1st/2nd/3rd, and a fixed plural would break 1. This
wording needs no language-specific change to the game code.

The race-result composer has its own original multi-line structure. Keep the
short labels `Your Time` and `Best Time`; the existing digit/time-format code
is unchanged.

## Validation and playthrough

Run the existing catalog validator from the project root:

```sh
python3 tools/validate_localization_catalog.py --language en --require-complete --strict-width
```

On 2026-09-19: 903/903 dialogue records, 105/105 game-text records, no errors
or width warnings. Additional review checked the loader's 80-character limit
for every record, all pickup name/suffix pairs and gadget counts 1 through 32.
The longest dialogue has 76 visible characters, the longest description 78.
This is catalog validation, not a substitute for a playthrough.

For the Windows check select `Game → Language → English`, restart after later
JSON edits, and inspect the opening, NPC dialogue, all 32 encyclopedia entries,
pickups, race results, bosses and ending. Verify the named speaker/portrait,
colors, line endings and cursor. Existing English PNG replacements are kept
unchanged (103 files); their book names were inspected. The user confirmed
the most complex scenes work correctly; a full in-game pass remains pending.
Record corrections by stable message ID.
