#ifndef __IRINGBUF_H__
#define __IRINGBUF_H__

#ifdef CONFIG_ITRACE

void iringbuf_init(void);
void iringbuf_push(const char *line);
void iringbuf_dump(void);

#else

static inline void iringbuf_init(void) { }
static inline void iringbuf_push(const char *line) { (void)line; }
static inline void iringbuf_dump(void) { }

#endif /* CONFIG_ITRACE */

#endif /* __IRINGBUF_H__ */