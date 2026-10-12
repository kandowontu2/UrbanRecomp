#ifndef SC_DISASTER_ART_H
#define SC_DISASTER_ART_H

/* Original Enhanced pixel art, approved 2026-10-11. Each character is one
 * native pixel; the renderer supplies the original-style 24px button bevel.
 * Palette order: background, pale highlight, black, navy, shadow, midtone,
 * white. The selected colors match the original disaster checkbox palette. */
static const unsigned sc_disaster_art_colors[2][7] = {
    {0xadadd6,0xc6c6de,0x000000,0x101031,0x292952,0x42426b,0xefffef},
    {0xffd673,0x00e7d6,0x000000,0x00009c,0x522900,0xff0000,0xffffff}
};
static const char sc_disaster_art[2][20][21] = {
    {
        "....m......s........",
        ".......s..mmm.......",
        ".....msm.wwsmm..m...",
        "..m.msmwwwhssm......",
        "...smwwwhmsswwsm....",
        "...mwwhssm.wwhssm...",
        "....mssms..whss.....",
        ".....sm....wws..s...",
        ".........smwws......",
        "..nsssn...swwnssn...",
        ".nwwhmsn.nwwkwhmsn..",
        "..wwhmsn..wwnkmsn...",
        "...whmsn...whkmsn...",
        "...whmsn...wknmsn...",
        "..wwhmssn.wwnwhmsn..",
        ".wwhhmssn.wwhkwhmsn.",
        "nwwhhmmssnwwhnwhmsn.",
        "wwwhhmmsswwhhhmsssn.",
        "..nnnnnnnnnnnnnnnn..",
        "..nwwnwwnnhwwhwssn.."
    },
    {
        "....................",
        ".......nnnn.........",
        "......nwwhmnn.......",
        "....nnwwwhhmsnn.....",
        "..nnhwwwwhhmmsnn....",
        ".nwwwhhhhmmssssnn...",
        "nnwhhmmssssnnnnnnn..",
        ".nnmssnwnnwnnwnnsnn.",
        "...nnnnnnnnnnnnnn...",
        "......nnnwwn........",
        "........w.w.........",
        ".........ww..m......",
        "..nnn....wwww.......",
        ".nwwhns...wwww......",
        "nnwwhns..nnwwwwn.s..",
        "nwwwhns.nwwnwwwwnn..",
        "nwnwhns.nwwhnwwwns..",
        "nwnwhns.nwnwhnwwww..",
        "nwwwhns.nwnwhns.w...",
        "nnnnnnn.nnnnnns....."
    }
};
static inline unsigned ScDisasterArtColorIndex(char pixel) {
    switch(pixel) {
    case 'h': return 1;
    case 'k': return 2;
    case 'n': return 3;
    case 's': return 4;
    case 'm': return 5;
    case 'w': return 6;
    default: return 0;
    }
}
#endif
