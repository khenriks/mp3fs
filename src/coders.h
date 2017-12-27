/*
 * Encoder and decoder interfaces for ffmpegfs
 *
 * Copyright (C) 2013 K. Henriksson
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

#ifndef CODERS_H
#define CODERS_H

#pragma once

/* Define lists of available encoder and decoder extensions. */
extern const char* encoder_list[];
extern const char* decoder_list[];

#ifdef __cplusplus
extern "C" {
#endif

/* Check for availability of audio types. */
int check_encoder(const char* type);
int check_decoder(const char* type);

#ifdef __cplusplus
}
#endif

#endif
