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
 * Data related to prefix / initial
 *
 * Make sure to coordinate with ALL functions that generate/modify these (below)
 * if you change the struct definition.
 */
struct FANSI_prefix_dat {
  const char * string;  // string translated to utf8
  int width;            // display width as computed by R_nchar
  int bytes;            // bytes, excluding NULL terminator
  // how many indent/exdent bytes are included in string, width, and bytes
  int indent;
  int has_utf8;         // whether utf8 contains value > 127
  int warn;             // warning issued while stripping
};
/*
 * Generate data related to prefix / initial
 */

static struct FANSI_prefix_dat make_pre(SEXP x) {
  SEXP chrsxp = STRING_ELT(x, 0);
  FANSI_check_chrsxp(chrsxp, 0);
  const char * x_utf8 = CHAR(chrsxp);
  // ideally we would IS_ASCII(x), but that's not available to extensions
  int x_has_utf8 = FANSI_has_utf8(x_utf8);

  // ideally would have an internal interface to strip so we don't need to
  // generate these SEXPs here
  SEXP warn = PROTECT(ScalarInteger(2));
  SEXP ctl = PROTECT(ScalarInteger(1));
  SEXP x_strip = PROTECT(FANSI_strip(x, ctl, warn));
  int x_width = R_nchar(
    asChar(x_strip), Width, TRUE, FALSE, "when computing display width"
  );
  // wish we could get this directly from R_nchar, grr

  int x_bytes = strlen(x_utf8);
  int warn_int = getAttrib(x_strip, FANSI_warn_sym) != R_NilValue;

  if(x_width == NA_INTEGER) {
    x_width = x_bytes;
    warn_int = 9;
  }
  UNPROTECT(3);
  return (struct FANSI_prefix_dat) {
    .string=x_utf8, .width=x_width, .bytes=x_bytes, .has_utf8=x_has_utf8,
    .indent=0, .warn=warn_int
  };
}
/*
 * Combine initial and indent (or prefix and exdent)
 */
static struct FANSI_prefix_dat pad_pre(
  struct FANSI_prefix_dat dat, int spaces
) {
  int pre_len = dat.bytes;
  const char * pre_chr = dat.string;

  int alloc_size = FANSI_ADD_INT(FANSI_ADD_INT(pre_len, spaces), 1);
  char * res_start = "";
  if(alloc_size > 1) {
    // Can't use buff here because we don't write this string out
    // Rprintf("Allocating pre size %d\n", alloc_size);

    char * res = res_start = R_alloc(alloc_size, sizeof(char));
    memcpy(res, pre_chr, pre_len);
    res += pre_len;
    for(int i = 0; i < spaces; ++i) *(res++) = ' ';
    *res = '\0';
  }
  dat.string = (const char *) res_start;
  dat.bytes = FANSI_ADD_INT(dat.bytes, spaces);
  dat.width = FANSI_ADD_INT(dat.width, spaces);
  dat.indent = FANSI_ADD_INT(dat.indent, spaces);

  return dat;
}
/*
 * Adjusts width and sizes to pretend there is no indent.  String itself is not
 * modified so this only works if whatever is using the string is using the byte
 * counter to limit how much of the string it reads
 */

static struct FANSI_prefix_dat drop_pre_indent(struct FANSI_prefix_dat dat) {
  dat.bytes = FANSI_ADD_INT(dat.bytes, -dat.indent);
  dat.width = FANSI_ADD_INT(dat.width, -dat.indent);
  dat.indent = FANSI_ADD_INT(dat.indent, -dat.indent);
  if(dat.indent < 0)
    // nocov start
    error(
      "Internal Error: cannot drop indent when there is none; contact ",
      "maintainer."
    );
    // nocov end
  return dat;
}
/*
 * Write a line
 *
 * @param state_bound the point where the boundary is
 * @param state_start the starting point of the line
 */

SEXP FANSI_writeline(
  struct FANSI_state state_bound, struct FANSI_state state_start,
  struct FANSI_buff * buff,
  struct FANSI_prefix_dat pre_dat,
  int tar_width, const char * pad_chr
) {
  // Rprintf("  Writeline start with buff %p\n", *buff);

  // Check if we are in a CSI state b/c if we are we neeed extra room for
  // the closing state tag

  int needs_close = FANSI_state_has_style(state_bound);
  int needs_start = FANSI_state_has_style(state_start);

  // state_bound.pos_byte 1 past what we need, so this should include room
  // for NULL terminator

  if(
    (state_bound.pos_byte < state_start.pos_byte) ||
    (state_bound.pos_width < state_start.pos_width)
  )
    // nocov start
    error("Internal Error: boundary leading position; contact maintainer.");
    // nocov end

  if(tar_width < 0) tar_width = 0;

  size_t target_size = state_bound.pos_byte - state_start.pos_byte;
  size_t target_width = state_bound.pos_width - state_start.pos_width;
  int target_pad = 0;

  if(!target_size) {
    // handle corner case for empty strings that don't get indented by strwrap;
    // we considered testing width instead of size as that would also prevent
    // indent on thing that just have ESCs, but decided against it (arbitrarily)
    //
    // We do not re-terminate the string, instead relying on widths / sizes to
    // make sure only the non-indent bit is copied

    pre_dat = drop_pre_indent(pre_dat);
  }
  // If we are going to pad the end, adjust sizes and widths

  if(target_size > (size_t) FANSI_int_max)
    // Not possible for this to be longer than INT_MAX as we check on
    // entry with FANSI_check_chrsxp and we're not expanding anything.
    // nocov start, but jut in cae
    error(
      "Substring to write (%ju) is longer than INT_MAX.",
      (uintmax_t) target_size
    );
    // nocov end

  if(target_width <= (size_t) tar_width && *pad_chr) {
    target_pad = tar_width - target_width;
    if(
      (target_size > (size_t) (FANSI_int_max - target_pad))
    ) {
      error(
        "%s than INT_MAX while padding.",
        "Attempting to create string longer"
      );
    }
    target_size = target_size + target_pad;
  }
  if(target_size > (size_t)(FANSI_int_max - pre_dat.bytes)) {
    error(
      "%s%s",
      "Attempting to create string longer than INT_MAX when adding ",
      "prefix/initial/indent/exdent."
    );
  }
  target_size += pre_dat.bytes;
  int state_start_size = 0;
  int start_close = 0;

  if(needs_close) start_close += 4;
  if(needs_start) {
    state_start_size = FANSI_state_size(state_start);
    start_close += state_start_size;  // this can't possibly overflow
  }
  if(target_size > (size_t)(FANSI_int_max - start_close)) {
    error(
      "%s%s",
      "Attempting to create string longer than INT_MAX while adding leading ",
      "and trailing CSI SGR sequences."
    );
  }
  target_size += start_close;
  ++target_size; // for NULL terminator

  // Make sure buffer is large enough
  FANSI_size_buff(buff, target_size);

  char * buff_track = buff->buff;

  // Apply prevous CSI style

  if(needs_start) {
    // Rprintf("  writing start: %d\n", state_start_size);
    FANSI_csi_write(buff_track, state_start, state_start_size);
    buff_track += state_start_size;
  }
  // Apply indent/exdent prefix/initial

  if(pre_dat.bytes) {
    // Rprintf("  writing pre %s of size %d\n", pre, pre_size);
    memcpy(buff_track, pre_dat.string, pre_dat.bytes);
    buff_track += pre_dat.bytes;
  }
  // Actual string, remember state_bound.pos_byte is one past what we need
  // (but what if we're in strip.space=FALSE?)

  memcpy(
    buff_track, state_start.string + state_start.pos_byte,
    state_bound.pos_byte - state_start.pos_byte
  );
  buff_track += state_bound.pos_byte - state_start.pos_byte;

  // Add padding if needed

  while(target_pad--) {
    *(buff_track++) = *pad_chr;
  }
  // And turn off CSI styles if needed

  if(needs_close) {
    // Rprintf("  close\n");
    memcpy(buff_track, "\033[0m", 4);
    buff_track += 4;
  }
  *buff_track = 0;
  // Rprintf("written %d\n", buff_track - (buff->buff) + 1);

  // Now create the charsxp and append to the list, start by determining
  // what encoding to use.  If pos_byte is greater than pos_ansi it means
  // we must have hit a UTF8 encoded character

  cetype_t chr_type = CE_NATIVE;
  if((state_bound.has_utf8 || pre_dat.has_utf8)) chr_type = CE_UTF8;

  if(buff_track - buff->buff > FANSI_int_max)
    // nocov start
    error(
      "%s%s",
      "Internal Error: attempting to write string longer than INT_MAX; ",
      "contact maintainer (4)."
    );
    // nocov end
  SEXP res_sxp = PROTECT(
    mkCharLenCE(
      buff->buff, (int) (buff_track - buff->buff), chr_type
  ) );
  UNPROTECT(1);
  return res_sxp;
}
/*
 * All input strings are expected to be in UTF8 compatible format (i.e. either
 * encoded in UTF8, or contain only bytes in 0-127).  That way we know we can
 * set the encoding to UTF8 if there are any bytes greater than 127, or NATIVE
 * otherwise under the assumption that 0-127 is valid in all encodings.
 *
 * @param buff a pointer to a buffer struct.  We use pointer to a
 *   pointer because it may need to be resized, but we also don't want to
 *   re-allocate the buffer between calls.
 * @param pre_first, pre_next, strings (and associated meta data) to prepend to
 *   each line; pre_first can be based of of `prefix` or off of `initial`
 *   depending whether we're at the very first line of the external input or not
 * @param strict whether to hard wrap at width or not (not is what strwrap does
 *   by default)
 */

static SEXP strwrap(
  const char * x, int width,
  struct FANSI_prefix_dat pre_first,
  struct FANSI_prefix_dat pre_next,
  int wrap_always,
  struct FANSI_buff * buff,
  const char * pad_chr,
  int strip_spaces,
  SEXP warn, SEXP term_cap,
  int first_only, SEXP ctl
) {
  SEXP R_true = PROTECT(ScalarLogical(1));
  SEXP R_one = PROTECT(ScalarInteger(1));
  struct FANSI_state state = FANSI_state_init_full(
    x, warn, term_cap, R_true, R_true, R_one, ctl
  );
  UNPROTECT(2);

  int width_1 = FANSI_ADD_INT(width, -pre_first.width);
  int width_2 = FANSI_ADD_INT(width, -pre_next.width);

  int width_tar = width_1;

  if(width < 1 && wrap_always)
    error("Internal Error: invalid width."); // nocov
  if(wrap_always && (width_1 < 0 || width_2 < 0))
    error("Internal Error: incompatible width/indent/prefix."); // nocov

  // Use LISTSXP so we don't have to do a two pass process to determine how many
  // items we're going to have, unless we're in first only in which case we know
  // we only need one element per and don't actually use these

  SEXP char_list_start, char_list;
  char_list_start = char_list = PROTECT(list1(R_NilValue));

  int prev_boundary = 0;    // tracks if previous char was a boundary
  int has_boundary = 0;     // tracks if at least one boundary in a line
  int para_start = 1;

  // byte we previously wrote from, need to track to detect potential infinite
  // loop when we wrap-always but the wrap width is narrower than a wide
  // character

  int first_line = 1;
  int last_start = 0;

  // Need to keep track of where word boundaries start and end due to
  // possibility for multiple elements between words

  struct FANSI_state state_start, state_bound, state_prev;
  state_start = state_bound = state_prev = state;
  R_xlen_t size = 0;
  SEXP res_sxp;

  while(1) {
    struct FANSI_state state_next;

    // Can no longer advance after we reach end, but we still need to assemble
    // strings so we assign `state` even though technically not correct

    if(!state.string[state.pos_byte]){
      state_next = state;
    } else {
      state_next = FANSI_read_next(state);
    }
    state.warn = state_bound.warn = state_next.warn;  // avoid double warning

    // detect word boundaries and paragraph starts; we need to track
    // state_bound for the special case where we are in strip space mode
    // and we happen to hit the width in a two space sequence such as we might
    // get after [.!?].

    if(
      state.string[state.pos_byte] == ' ' ||
      state.string[state.pos_byte] == '\t' ||
      state.string[state.pos_byte] == '\n'
    ) {
      // Rprintf(
      //   "Bound @ %d raw: %d chr: %d prev: %d\n",
      //   state.pos_byte - state_start.pos_byte, state.pos_byte,
      //   state.string[state.pos_byte], prev_boundary
      // );
      if(strip_spaces && !prev_boundary) state_bound = state;
      else if(!strip_spaces) state_bound = state;
      has_boundary = prev_boundary = 1;
    } else {
      prev_boundary = 0;
    }
    // Write the line

    if(
      !state.string[state.pos_byte] ||
      // newlines kept in strtrim mode
      (state.string[state.pos_byte] == '\n' && !first_only) ||
      (
        (
          state.pos_width > width_tar ||
          (
            // If exactly at width we need to keep going if the next char is
            // zero width, otherwise we should write the string
            state.pos_width == width_tar &&
            state_next.pos_width > state.pos_width
        ) ) &&
        (has_boundary || wrap_always)
      )
    ) {
      if(
        !state.string[state.pos_byte] ||
        (wrap_always && !has_boundary) || first_only
      ) {
        if(state.pos_width > width_tar && wrap_always) {
          state = state_prev; // wide char overshoot
        }
        state_bound = state;
      }
      if(!first_line && last_start >= state_start.pos_byte) {
        error(
          "%s%s",
          "Wrap error: trying to wrap to width narrower than ",
          "character width; set `wrap.always=FALSE` to resolve."
        );
      }
      // If not stripping spaces we need to keep the last boundary char; note
      // that boundary is advanced when strip_spaces == FALSE in earlier code.

      if(
        !strip_spaces && has_boundary && (
          state_bound.string[state_bound.pos_byte] == ' ' ||
          state_bound.string[state_bound.pos_byte] == '\t'
        ) &&
        state_bound.pos_byte < state.pos_byte
      ) {
        state_bound = FANSI_read_next(state_bound);
      }
      // Write the string

      res_sxp = PROTECT(
        FANSI_writeline(
          state_bound, state_start, buff,
          para_start ? pre_first : pre_next,
          width_tar, pad_chr
        )
      );
      first_line = 0;
      last_start = state_start.pos_byte;
      // first_only for `strtrim`

      if(!first_only) {
        SETCDR(char_list, list1(res_sxp));
        char_list = CDR(char_list);
        UNPROTECT(1);
      } else break;
      // overflow should be impossible here since string is at most int long

      ++size;
      if(!state.string[state.pos_byte]) break;

      // Next line will be the beginning of a paragraph

      para_start = (state.string[state.pos_byte] == '\n');
      width_tar = para_start ? width_1 : width_2;

      // Recreate what the state is at the wrap point, including skipping the
      // wrap character if there was one, and any subsequent leading spaces if
      // there are any and we are in strip_space mode.  If there was no boundary
      // then we're hard breaking and we reset position to the next position.

      // Rprintf(
      //   "Positions has_b: %d, state: %d bound: %d prev: %d next: %d\n",
      //   has_boundary,
      //   state.pos_byte, state_bound.pos_byte, state_prev.pos_byte,
      //   state_next.pos_byte
      // );
      if(has_boundary && para_start) {
        state_bound = FANSI_read_next(state_bound);
      } else if(!has_boundary) {
        state_bound = state;
      }
      if(strip_spaces) {
        while(state_bound.string[state_bound.pos_byte] == ' ') {
          state_bound = FANSI_read_next(state_bound);
      } }
      has_boundary = 0;
      state_bound.pos_width = 0;

      state_prev = state;
      state = state_start = state_bound;
    } else {
      state_prev = state;
      state = state_next;
    }
  }
  // Convert to string and return; this is a little inefficient for the
  // `first_only` mode as ideally we would just return a CHARSXP, but for now we
  // are just trying to keep it simple

  SEXP res;

  if(!first_only) {
    res = PROTECT(allocVector(STRSXP, size));
    char_list = char_list_start;
    for(R_xlen_t i = 0; i < size; ++i) {
      char_list = CDR(char_list); // first element is NULL
      if(char_list == R_NilValue)
        error("Internal Error: wrapped element count mismatch");  // nocov
      SET_STRING_ELT(res, i, CAR(char_list));
    }
    if(CDR(char_list) != R_NilValue)
      error("Internal Error: wrapped element count mismatch 2");  // nocov
  } else {
    // recall there is an extra open PROTECT in first_only mode
    res = res_sxp;
  }
  UNPROTECT(2);
  return res;
}

/*
 * All integer inputs are expected to be positive, which should be enforced by
 * the R interface checks.
 *
 * @param strict whether to force a hard cut in-word when a full word violates
 *   the width limit on its own
 * @param first_only whether we only want the first line of a wrapped element,
 *   this is to support strtrim. If this is true then the return value becomes a
 *   character vector (STRSXP) rather than a VECSXP
 */

SEXP FANSI_strwrap_ext(
  SEXP x, SEXP width,
  SEXP indent, SEXP exdent,
  SEXP prefix, SEXP initial,
  SEXP wrap_always, SEXP pad_end,
  SEXP strip_spaces,
  SEXP tabs_as_spaces, SEXP tab_stops,
  SEXP warn, SEXP term_cap,
  SEXP first_only,
  SEXP ctl
) {
  if(
    TYPEOF(x) != STRSXP || TYPEOF(width) != INTSXP ||
    TYPEOF(indent) != INTSXP || TYPEOF(exdent) != INTSXP ||
    TYPEOF(prefix) != STRSXP || TYPEOF(initial) != STRSXP ||
    TYPEOF(wrap_always) != LGLSXP ||
    TYPEOF(pad_end) != STRSXP ||
    TYPEOF(warn) != LGLSXP || TYPEOF(term_cap) != INTSXP ||
    TYPEOF(strip_spaces) != LGLSXP ||
    TYPEOF(tabs_as_spaces) != LGLSXP ||
    TYPEOF(tab_stops) != INTSXP ||
    TYPEOF(first_only) != LGLSXP ||
    TYPEOF(ctl) != INTSXP
  )
    error("Internal Error: arg type error 1; contact maintainer.");  // nocov

  const char * pad = CHAR(asChar(pad_end));
  if(*pad != 0 && (*pad < 0x20 || *pad > 0x7e))
    error(
      "%s%s",
      "Argument `pad.end` must be an empty string or a single ",
      "printable ASCII character."
    );

  // Set up the buffer, this will be created in FANSI_strwrap, but we want a
  // handle for it here so we can re-use

  struct FANSI_buff buff = {.len = 0};

  // Strip whitespaces as needed; `strwrap` doesn't seem to do this with prefix
  // and initial, so we don't either

  int strip_spaces_int = asInteger(strip_spaces);

  if(strip_spaces_int) x = PROTECT(FANSI_process(x, &buff));
  else PROTECT(x);

  // and tabs

  if(asInteger(tabs_as_spaces)) {
    x = PROTECT(FANSI_tabs_as_spaces(x, tab_stops, &buff, warn, term_cap, ctl));
    prefix = PROTECT(
      FANSI_tabs_as_spaces(prefix, tab_stops, &buff, warn, term_cap, ctl)
    );
    initial = PROTECT(
      FANSI_tabs_as_spaces(initial, tab_stops, &buff, warn, term_cap, ctl)
    );
  }
  else x = PROTECT(PROTECT(PROTECT(x)));  // PROTECT stack balance

  // Prepare the leading strings; could turn out to be wasteful if we don't
  // need them all; there are three possible combinations: 1) first line of the
  // entire input with indent, 2) first line of paragraph with prefix and
  // indent, 3) other lines with prefix and exdent.

  struct FANSI_prefix_dat pre_dat_raw, ini_dat_raw,
    ini_first_dat, pre_first_dat, pre_next_dat;

  int indent_int = asInteger(indent);
  int exdent_int = asInteger(exdent);
  int warn_int = asInteger(warn);
  int first_only_int = asInteger(first_only);

  if(indent_int < 0 || exdent_int < 0)
    error("Internal Error: illegal indent/exdent values.");  // nocov

  pre_dat_raw = make_pre(prefix);

  const char * warn_base =
    "`%s` contains unhandled ctrl or UTF-8 sequences (see `?unhandled_ctl`).";
  if(warn_int && pre_dat_raw.warn) warning(warn_base, "prefix");
  if(prefix != initial) {
    ini_dat_raw = make_pre(initial);
    if(warn_int && ini_dat_raw.warn) warning(warn_base, "initial");
  } else ini_dat_raw = pre_dat_raw;

  ini_first_dat = pad_pre(ini_dat_raw, indent_int);

  if(initial != prefix) {
    pre_first_dat = pad_pre(pre_dat_raw, indent_int);
  } else pre_first_dat = ini_first_dat;

  if(indent_int != exdent_int) {
    pre_next_dat = pad_pre(pre_dat_raw, exdent_int);
  } else pre_next_dat = pre_first_dat;

  // Check that widths are feasible, although really only relevant if in strict
  // mode

  int width_int = asInteger(width);
  int wrap_always_int = asInteger(wrap_always);

  if(
    wrap_always_int && (
      ini_first_dat.width >= width_int ||
      pre_first_dat.width >= width_int ||
      pre_next_dat.width >= width_int
    )
  )
    error(
      "%s%s",
      "Width error: sum of `indent` and `initial` width or sum of `exdent` ",
      "and `prefix` width must be less than `width - 1` when in `wrap.always`."
    );

  // Could be a little faster avoiding this allocation if it turns out nothing
  // needs to be wrapped and we're in simplify=TRUE, but that seems like a lot
  // of work for a rare event

  R_xlen_t i, x_len = XLENGTH(x);
  SEXP res;

  if(first_only_int) {
    // this is to support trim mode
    res = PROTECT(allocVector(STRSXP, x_len));
  } else {
    res = PROTECT(allocVector(VECSXP, x_len));
  }
  // Wrap each element

  for(i = 0; i < x_len; ++i) {
    FANSI_interrupt(i);
    SEXP chr = STRING_ELT(x, i);
    if(chr == NA_STRING) continue;
    FANSI_check_chrsxp(chr, i);
    const char * chr_utf8 = CHAR(chr);

    SEXP str_i = PROTECT(
      strwrap(
        chr_utf8, width_int,
        i ? pre_first_dat : ini_first_dat,
        pre_next_dat,
        wrap_always_int, &buff,
        CHAR(asChar(pad_end)),
        strip_spaces_int,
        warn, term_cap,
        first_only_int,
        ctl
    ) );
    if(first_only_int) {
      SET_STRING_ELT(res, i, str_i);
    } else {
      SET_VECTOR_ELT(res, i, str_i);
    }
    UNPROTECT(1);
  }
  UNPROTECT(5);
  return res;
}
