/*
 * Copyright (C) 2021  Brodie Gaslam
 *
 * This file is part of "fansi - ANSI Control Sequence Aware String Functions"
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Go to <https://www.r-project.org/Licenses/GPL-2> for a copy of the license.
 */

#include "fansi.h"

/*
 * Determine how many spaces tab width should be
 *
 * state should be at a tab
 */
int FANSI_tab_width(struct FANSI_state state, SEXP tab_stops) {
  R_xlen_t stops = XLENGTH(tab_stops);
  if(!stops)
    error("Internal Error: must have at least one tab stop");  // nocov
  if(*(state.string + state.pos_byte) != '\t')
    error("Internal Error: computing tab width on not a tab"); // nocov

  int tab_width = 0;
  R_xlen_t stop_idx = 0;

  while(state.pos_width >= tab_width) {
    int stop_size = INTEGER(tab_stops)[stop_idx];
    if(stop_size < 1)
      error("Internal Error: stop size less than 1.");  // nocov
    if(tab_width > FANSI_int_max - stop_size)
      error("Integer overflow when attempting to compute tab width."); // nocov
    tab_width += stop_size;
    if(stop_idx < stops - 1) stop_idx++;
  }
  return tab_width - state.pos_width;
}

SEXP FANSI_tabs_as_spaces(
  SEXP vec, SEXP tab_stops, struct FANSI_buff * buff,  SEXP warn,
  SEXP term_cap, SEXP ctl
) {
  if(TYPEOF(vec) != STRSXP)
    error("Argument 'vec' should be a character vector"); // nocov
  R_xlen_t len = XLENGTH(vec);
  R_xlen_t len_stops = XLENGTH(tab_stops);

  const char * source;
  int tabs_in_str = 0;
  int max_tab_stop = 1;

  SEXP res_sxp = vec;

  PROTECT_INDEX ipx;
  PROTECT_WITH_INDEX(res_sxp, &ipx);  // reserve spot if we need to alloc later

  for(R_xlen_t i = 0; i < len; ++i) {
    FANSI_interrupt(i);
    int tab_count = 0;

    SEXP chr = STRING_ELT(vec, i);
    if(chr == NA_STRING) continue;
    FANSI_check_chrsxp(chr, i);

    source = CHAR(chr);

    while(*source && (source = strchr(source, '\t'))) {
      if(!tabs_in_str) {
        tabs_in_str = 1;
        REPROTECT(res_sxp = duplicate(vec), ipx);
        for(R_xlen_t j = 0; j < len_stops; ++j) {
          if(INTEGER(tab_stops)[j] > max_tab_stop)
            max_tab_stop = INTEGER(tab_stops)[j];
        }
      }
      ++tab_count;
      ++source;
    }
    if(tab_count) {
      // Need to convert to UTF8 so width calcs work

      const char * string = CHAR(chr);

      // Figure out possible size of buffer, allowing max_tab_stop for every
      // tab, which should over-allocate

      size_t new_buff_size = LENGTH(chr);
      int tab_extra = max_tab_stop - 1;

      for(int k = 0; k < tab_count; ++k) {
        if(new_buff_size > (size_t) (FANSI_int_max - tab_extra))
          error(
            "%s%s",
            "Converting tabs to spaces will cause string to be longer than ",
            "allowed INT_MAX."
          );
        new_buff_size += tab_extra;
      }
      ++new_buff_size;   // Room for NULL

      FANSI_size_buff(buff, new_buff_size);

      SEXP R_true = PROTECT(ScalarLogical(1));
      SEXP R_one = PROTECT(ScalarInteger(1));
      struct FANSI_state state = FANSI_state_init_full(
        string, warn, term_cap, R_true, R_true, R_one, ctl
      );
      UNPROTECT(2);

      char cur_chr;

      char * buff_track, * buff_start;
      buff_track = buff_start = buff->buff;

      int last_byte = state.pos_byte;
      int warn_old = state.warn;

      while(1) {
        cur_chr = state.string[state.pos_byte];
        int extra_spaces = 0;

        if(cur_chr == '\t') {
          extra_spaces = FANSI_tab_width(state, tab_stops);
        } else if (cur_chr == '\n') {
          state = FANSI_reset_width(state);
        }
        // Write string

        if(cur_chr == '\t' || !cur_chr) {
          int write_bytes = state.pos_byte - last_byte;
          memcpy(buff_track, state.string + last_byte, write_bytes);
          buff_track += write_bytes;

          // consume tab and advance

          state.warn = 0;
          state = FANSI_read_next(state);
          state.warn = warn_old;
          cur_chr = state.string[state.pos_byte];
          state = FANSI_inc_width(state, extra_spaces);
          last_byte = state.pos_byte;

          // actually write the extra spaces

          while(extra_spaces) {
            --extra_spaces;
            *buff_track = ' ';
            ++buff_track;
          }
          if(!cur_chr) *buff_track = 0;
        }
        if(!cur_chr) break;
        state = FANSI_read_next(state);
      }
      // Write the CHARSXP

      cetype_t chr_type = CE_NATIVE;
      if(state.has_utf8) chr_type = CE_UTF8;
      FANSI_check_chr_size(buff_start, buff_track, i);
      SEXP chr_sxp = PROTECT(
        mkCharLenCE(buff_start, (int) (buff_track - buff_start), chr_type)
      );
      SET_STRING_ELT(res_sxp, i, chr_sxp);
      UNPROTECT(1);
    }
  }
  UNPROTECT(1);
  return res_sxp;
}
SEXP FANSI_tabs_as_spaces_ext(
  SEXP vec, SEXP tab_stops, SEXP warn, SEXP term_cap, SEXP ctl
) {
  struct FANSI_buff buff = {.len = 0};

  return FANSI_tabs_as_spaces(vec, tab_stops, &buff, warn, term_cap, ctl);
}

