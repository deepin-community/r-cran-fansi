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
#include <R_ext/Rdynload.h>

static const
R_CallMethodDef callMethods[] = {
  {"has_csi", (DL_FUNC) &FANSI_has, 3},
  {"strip_csi", (DL_FUNC) &FANSI_strip, 3},
  {"strwrap_csi", (DL_FUNC) &FANSI_strwrap_ext, 15},
  {"state_at_pos_ext", (DL_FUNC) &FANSI_state_at_pos_ext, 8},
  {"process", (DL_FUNC) &FANSI_process_ext, 1},
  {"check_assumptions", (DL_FUNC) &FANSI_check_assumptions, 0},
  {"digits_in_int", (DL_FUNC) &FANSI_digits_in_int_ext, 1},
  {"tabs_as_spaces", (DL_FUNC) &FANSI_tabs_as_spaces_ext, 5},
  {"color_to_html", (DL_FUNC) &FANSI_color_to_html_ext, 1},
  {"esc_to_html", (DL_FUNC) &FANSI_esc_to_html, 4},
  {"unhandled_esc", (DL_FUNC) &FANSI_unhandled_esc, 2},
  {"unique_chr", (DL_FUNC) &FANSI_unique_chr, 1},
  {"nzchar_esc", (DL_FUNC) &FANSI_nzchar, 5},
  {"add_int", (DL_FUNC) &FANSI_add_int_ext, 2},
  {"strsplit", (DL_FUNC) &FANSI_strsplit, 3},
  {"cleave", (DL_FUNC) &FANSI_cleave, 1},
  {"order", (DL_FUNC) &FANSI_order, 1},
  {"sort_int", (DL_FUNC) &FANSI_sort_int, 1},
  {"sort_chr", (DL_FUNC) &FANSI_sort_chr, 1},
  {"set_int_max", (DL_FUNC) &FANSI_set_int_max, 1},
  {"get_int_max", (DL_FUNC) &FANSI_get_int_max, 0},
  {"check_enc", (DL_FUNC) &FANSI_check_enc_ext, 2},
  {"ctl_as_int", (DL_FUNC) &FANSI_ctl_as_int_ext, 1},
  {"esc_html", (DL_FUNC) &FANSI_esc_html, 1},
  {NULL, NULL, 0}
};

SEXP FANSI_warn_sym;

void R_init_fansi(DllInfo *info)
{
 /* Register the .C and .Call routines.
    No .Fortran() or .External() routines,
    so pass those arrays as NULL.
  */
  R_registerRoutines(info, NULL, callMethods, NULL, NULL);
  R_useDynamicSymbols(info, FALSE);
  R_forceSymbols(info, FALSE);

  FANSI_warn_sym = install("warn");
}

