#ifndef WIKITEXT_H
#define WIKITEXT_H

/*
 * Turns a Ross-Tech wiki page's raw MediaWiki source (index.php?action=raw)
 * into plain lines for a small screen. Each output line starts with a marker:
 *
 *   "= "  page title          "# "  section heading
 *   "* "  list item           "** " nested list item
 *   "  "  paragraph text
 *
 * Links keep their text, templates, categories, files, tables and tags are
 * dropped, and the text is limited to ASCII plus the degree sign, which the
 * P4's fonts have. Returns the length written (always NUL-terminated).
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t kw_wikitext_render(const char *src, size_t src_len, char *out, size_t out_len);

/*
 * If the page is a redirect ("#REDIRECT [[17524/P1116/004374]]"), copies the
 * target title into `out` and returns 1; otherwise returns 0.
 */
int kw_wikitext_redirect(const char *src, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif
