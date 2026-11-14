/* An inline function with a static local: each image that uses it carries a weak
 * definition, and dyld must coalesce them so that all images share one counter. */
inline int &shared_counter() { static int c; return c; }
