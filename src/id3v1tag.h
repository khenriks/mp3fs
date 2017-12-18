/*
 * ID3v1 tag structure
 *
 * Copyright (C) 2017 Norbert Schlia (nschlia@oblivion-software.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef ID3V1TAG_H
#define ID3V1TAG_H

#pragma once

#include <memory.h>

struct ID3v1
{
    char m_sTAG[3];         // Contains "TAG"
    char m_sSongTitle[30];  // Title of sound track
    char m_sSongArtist[30]; // Artist Name
    char m_sAlbumName[30];  // Album Name
    char m_sYear[4];        // Year of publishing
    char m_sComment[28];    // Any user comments
    char m_bPad;            // Must be '\0'
    char m_bTitleNo;
    char m_bGenre;          // Type of music
};

extern void init_id3v1(ID3v1 *id3v1);

#define ID3V1_TAG_LENGTH sizeof(ID3v1)  // 128 bytes

#endif // ID3V1TAG_H
