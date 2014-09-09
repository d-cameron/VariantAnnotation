#include "writevcf.h"
#include "samtools/kstring.h"

/* write all elements of 'list' genotype field */
static void write_list_elt(SEXP v_elt, const char *mv_sep, kstring_t *bufp) 
{
    SEXPTYPE v_type;
    int v, v_len;

    v_type = TYPEOF(v_elt);
    v_len = length(v_elt);
    switch (v_type) {
        case NILSXP:
            break;
        case LGLSXP:
            Rf_warning("'logical' is not a valid FORMAT data type");
            break;
        case INTSXP:
            for (v = 0; v < v_len; v++) {
                if (NA_INTEGER != INTEGER(v_elt)[v]) 
                    ksprintf(bufp, "%d", INTEGER(v_elt)[v]);
                else
                    ksprintf(bufp, "%s", ".");
                if (v < v_len - 1)
                    ksprintf(bufp, "%s", mv_sep);
            }
            break;
        case REALSXP:
            for (v = 0; v < v_len; v++) {
                if (NA_REAL !=  REAL(v_elt)[v])
                    ksprintf(bufp, "%g", REAL(v_elt)[v]);
                else
                    ksprintf(bufp, "%s", ".");
                if (v < v_len - 1)
                    ksprintf(bufp, "%s", mv_sep);
            }
            break;
        case STRSXP:
            for (v = 0; v < v_len; v++) {
                if (NA_STRING != STRING_ELT(v_elt, v))
                    ksprintf(bufp, "%s", CHAR(STRING_ELT(v_elt, v)));
                else
                    ksprintf(bufp, "%s", ".");
                if (v < v_len - 1)
                    ksprintf(bufp, "%s", mv_sep);
            }
            break;
        default:
            Rf_error("unsupported 'geno' type: %s", type2char(v_type));
            break;
    }
}

/* write all genotype fields for a single sample */
static void write_geno_sample(int i, int j, int k_last, Rboolean *k_valid,
                              SEXP geno_zdim, int n_rows, int n_samples,
                              int n_fields, SEXP geno, const char *f_sep,
                              const char *mv_sep, kstring_t *bufp)
{
    SEXP field, c_elt, v_elt;
    SEXPTYPE type;
    double d_elt;
    int k, z, z_dim, z_max, index, i_elt;
 
    for (k = 0; k < n_fields; ++k) {
        if (!k_valid[k])
            continue;
        field = VECTOR_ELT(geno, k);
        type = TYPEOF(field);
        z_dim = INTEGER(geno_zdim)[k];
        z_max = (NA_INTEGER != z_dim) ? z_dim : 1;
        for (z = 0; z < z_max; ++z) {
            index = i + j*n_rows + z*n_rows*n_samples; 
            switch (type) {
            case NILSXP:
                break;
            case LGLSXP:
                Rf_warning("'logical' is not a valid FORMAT data type");
                break;
            case INTSXP:
                i_elt = INTEGER(field)[index];
                if (NA_INTEGER != i_elt) 
                    ksprintf(bufp, "%d", i_elt);
                else
                    ksprintf(bufp, "%s", ".");
                break;
            case REALSXP:
                d_elt = REAL(field)[index];
                if (NA_REAL != d_elt)
                    ksprintf(bufp, "%g", d_elt);
                else
                    ksprintf(bufp, "%s", ".");
                break;
            case STRSXP:
                c_elt = STRING_ELT(field, index);
                if (NA_STRING != c_elt)
                    ksprintf(bufp, "%s", CHAR(c_elt));
                else
                    ksprintf(bufp, "%s", ".");
                break;
            case VECSXP:
                v_elt = VECTOR_ELT(field, index);
                write_list_elt(v_elt, mv_sep, bufp);
                break;
            default:
                Rf_error("unsupported 'geno' type: %s", type2char(type));
                break;
            }
            /* multi-value separator */
            if (z < z_max - 1 && k_valid[k])
                ksprintf(bufp, "%s", mv_sep);
        }
        /* field separator */
        if (k < n_fields - 1 && k < k_last)
            ksprintf(bufp, "%s", f_sep);
    }
}

/* return TRUE if genotype element is NA */
static Rboolean valid_geno_elt(SEXP field, int index)
{
    Rboolean valid = FALSE;
    Rboolean v_valid = FALSE;
    const SEXPTYPE type = TYPEOF(field);
    SEXP elt;
    int v;

    switch (type) {
        case NILSXP:
            break;
        case LGLSXP:
            valid = NA_INTEGER != LOGICAL(field)[index];
            break;
        case INTSXP:
            valid = NA_INTEGER != INTEGER(field)[index];
            break;
        case REALSXP:
            valid = NA_REAL != REAL(field)[index];
            break;
        case STRSXP:
            valid = NA_STRING != STRING_ELT(field, index);
            break;
        case VECSXP:
            //valid = valid_list_elt(VECTOR_ELT(field, index));
            elt = VECTOR_ELT(field, index);
            for (v = 0; v < length(elt); v++) {
                if (valid_geno_elt(elt, v)) {
                    v_valid = TRUE;
                    break;
                }
            }
            valid = v_valid;
            break;
        default:
            Rf_error("unsupported 'geno' type: %s", type2char(type));
            break;
    }
    return valid;
} 

/* --- .Call ENTRY POINT --- 
 * 'fixed'      : character vector of FIXED fields
 * 'format'     : character vector of FORMAT names
 * 'geno'       : list of genotype data
 * 'separators' : character vector of length 2 consisting of a field
 *                separator (first) and multi-value per field 
 *                separator (second)
 * 'vcf_dim'    : integer vector of length 2 (n rows, n cols)
 * 'geno_zdim'  : integer vector of z dimension of genotype data 
*/
SEXP make_vcf_geno(SEXP fixed, SEXP format, SEXP geno, SEXP separators,
                   SEXP vcf_dim, SEXP geno_zdim)
{
    const char *f_sep = CHAR(STRING_ELT(separators, 0));
    const char *mv_sep = CHAR(STRING_ELT(separators, 1));
    int n_rows = INTEGER(vcf_dim)[0];
    int n_samples = INTEGER(vcf_dim)[1];
    int n_fields = length(format);
    int i, j, k, k_last, z, z_dim, z_max;
    int index;
    Rboolean fmt_search, fmt_found, *k_valid;

    kstring_t buf;
    buf.l = buf.m = 0; buf.s = NULL;

    SEXP field, result; 

    if (n_fields != length(geno))
      Rf_error("length(format) must equal length(geno)");
    if (length(geno_zdim) != length(geno))
      Rf_error("length(geno_zdim) must equal length(geno)");

    k_valid = (Rboolean *) R_alloc(sizeof(Rboolean), n_fields);
    PROTECT(result = allocVector(STRSXP, n_rows));

    for (i = 0; i < n_rows; ++i) {
        /* reset buffer */
        buf.l = 0;
        if (NULL != buf.s) *buf.s = '\0';
        ksprintf(&buf, "%s\t", CHAR(STRING_ELT(fixed, i)));

        fmt_found = FALSE;
        k_last = 0;
        /* write format names */
        for (k = 0; k < n_fields; ++k) {
            fmt_search = TRUE;
            field = VECTOR_ELT(geno, k);
            z_dim = INTEGER(geno_zdim)[k];
            z_max = (NA_INTEGER != z_dim) ? z_dim : 1;
            for (j = 0; j < n_samples && fmt_search; ++j) {
                for (z = 0; z < z_max; ++z) {
                    index = i + j*n_rows + z*n_rows*n_samples; 
                    if (valid_geno_elt(field, index)) {
                        /* avoid trailing f_sep */
                        if (fmt_found)
                            ksprintf(&buf, "%s%s", f_sep,
                                     CHAR(STRING_ELT(format,k))); 
                        else
                            ksprintf(&buf, "%s", CHAR(STRING_ELT(format,k))); 
                        if (k == n_fields - 1)
                            ksprintf(&buf, "%s", "\t"); 
                        k_valid[k] = TRUE;
                        k_last = k;
                        fmt_search = FALSE;
                        fmt_found = TRUE;
                        break;
                    } else if (k == n_fields - 1 && 
                               z == z_max - 1 &&
                               j == n_samples - 1) {
                        ksprintf(&buf, "%s", "\t");
                        k_valid[k] = FALSE;
                    } else k_valid[k] = FALSE;
                }
            }
        }
        /* write genotype fields */
        for (j = 0; j < n_samples; ++j) {
            write_geno_sample(i, j, k_last, k_valid, geno_zdim, n_rows, 
                              n_samples, n_fields, geno, f_sep, mv_sep, 
                              &buf);
            if (j < n_samples - 1) {
                if (0 != buf.l)
                    /* sample separator */
                    ksprintf(&buf, "%s", "\t");
            } else {
                SET_STRING_ELT(result, i, mkChar(buf.s));
            }
        }
    }

    free(buf.s);
    UNPROTECT(1);
    return result;
}

/* paste-collapse character matrix by row, ignoring NAs */
SEXP matrix_pasteCollapseRows(SEXP x, SEXP sep) {
  /* VO: looks like 'nc' is not used?
   * int nc = ncols(x), nr = nrows(x);
   */
  int nr = nrows(x);
  char c_sep = CHAR(STRING_ELT(sep, 0))[0];

  SEXP ans = allocVector(STRSXP, nr);
  PROTECT(ans);

  for (int r = 0; r < nr; r++) {
    int len = 0;
    for (int i = r; i < length(x); i += nr) {
      SEXP str = STRING_ELT(x, i);
      if (str != NA_STRING)
        len += length(str) + 1;
    }
    char *collapsed = R_alloc(sizeof(char), len);
    char *dest = collapsed;
    for (int i = r; i < length(x); i += nr) {
      SEXP str = STRING_ELT(x, i);
      if (str != NA_STRING) {
        strcpy(dest, CHAR(str));
        dest[length(str)] = c_sep;
        dest += length(str) + 1;
      }
    }
    SET_STRING_ELT(ans, r, mkCharLen(collapsed, len - 1 * (len > 0)));
  }

  UNPROTECT(1);
  return ans;
}