


#include <kdebug.h>
#include <klocale.h>
#include <malloc.h>
#include <qapplication.h>
#include <stdio.h>

#include "font.h"
#include "kdvi.h"

extern "C" {
#include <kpathsea/c-fopen.h>
}


#include "oconfig.h"

extern FILE *xfopen(const char *filename, const char *type);
extern void oops(QString message);
extern int n_files_left;

#define	PK_PRE		247
#define	PK_ID		89
#define	PK_MAGIC	(PK_PRE << 8) + PK_ID
#define	GF_PRE		247
#define	GF_ID		131
#define	GF_MAGIC	(GF_PRE << 8) + GF_ID
#define	VF_PRE		247
#define	VF_ID_BYTE	202
#define	VF_MAGIC	(VF_PRE << 8) + VF_ID_BYTE


void font::font_name_receiver(KProcess *proc, char *buffer, int buflen)
{
  if (buflen < 3)
    return;

  flags |= font::FONT_LOADED;

  filename = QString::fromLocal8Bit(buffer, buflen-1);

  kdDebug() << "FONT NAME RECEIVED:" << filename << endl;

  file = xfopen(filename.latin1(), FOPEN_R_MODE);
  if (file == NULL) {
    kdError() << i18n("Can't find font ") << fontname << "." << endl;
    return True;
  }

  --n_files_left;
  timestamp  = ++current_timestamp;
  set_char_p = &dviWindow::set_char;
  int magic      = two(file);

  if (magic == PK_MAGIC)
    read_PK_index();
  else
    if (magic == GF_MAGIC)
      oops(QString(i18n("The GF format for font file %1 is no longer supported")).arg(filename) );
    else
      if (magic == VF_MAGIC)
	read_VF_index();
      else
	oops(QString(i18n("Cannot recognize format for font file %1")).arg(filename) );
}


font::font(char *nfontname, float nfsize, long chk, int mag, double dconv)
{
#ifdef DEBUG_FONT
  kdDebug() << "constructing font " << nfontname << " at " << (int) (nfsize + 0.5) << " dpi" << endl;
#endif

  fontname   = nfontname;
  fsize      = nfsize;
  checksum   = chk;
  magstepval = mag;
  flags      = font::FONT_IN_USE || font::FONT_NEEDS_TO_BE_LOADED;
  file       = NULL; 
  filename   = "";
  dimconv    = dconv;
}

font::~font()
{
#ifdef DEBUG_FONT
  kdDebug() << "discarding font " << fontname << " at " << (int)(fsize + 0.5) << " dpi" << endl;
#endif

  free(fontname);

  if (flags & FONT_LOADED) {
    if (file != NULL) {
      fclose(file);
      ++n_files_left;
    }

    if (flags & FONT_VIRTUAL) {
      for (macro *m = macrotable; m < macrotable + max_num_of_chars_in_font; ++m)
	if (m->free_me)
	  free((char *) m->pos);
      free((char *) macrotable);
      vf_table.clear();
    } else {
      for (glyph *g = glyphtable; g < glyphtable + max_num_of_chars_in_font; ++g)
	delete g;
      free((char *) glyphtable);
    }
  }
}



/** load_font locates the raster file and reads the index of
    characters, plus whatever other preprocessing is done (depending
    on the format).  */

unsigned char font::load_font(void)
{
#ifdef DEBUG_FONT
  kdDebug() << "loading font " <<  fontname << " at " << (int) (fsize + 0.5) << " dpi" << endl;
#endif

  // Find the font name. We use the external program "kpsewhich" for
  // that purpose.
  KShellProcess proc;
  connect(&proc, SIGNAL(receivedStdout(KProcess *, char *, int)),
	  this, SLOT(font_name_receiver(KProcess *, char *, int)));

  // First try if the font is a virtual font
  proc.clearArguments();
  proc << "kpsewhich";
  proc << "--dpi 600";
  proc << "--mode ljfour";
  proc << "--format vf";
  proc << fontname;
  proc.start(KProcess::NotifyOnExit, KProcess::All);
  while(proc.isRunning())
    qApp->processEvents();

  // Font not found? Then check if the font is a regular font.
  if (filename.isEmpty()) {
    proc.clearArguments();
    proc << "kpsewhich";
    proc << "--dpi 600";
    proc << "--mode ljfour";
    proc << "--format pk";
    proc << "--mktex pk";
    proc << QString("%1.%2").arg(fontname).arg((int)(fsize + 0.5));
    proc.start(KProcess::NotifyOnExit, KProcess::All);
    while(proc.isRunning())
      qApp->processEvents();
  }


  return False;
}



/** mark_as_used marks the font, and all the fonts it referrs to, as
    used, i.e. their FONT_IN_USE-flag is set. */

void font::mark_as_used(void)
{
#ifdef DEBUG_FONT
  kdDebug() << "marking font " << fontname << " at " << (int) (fsize + 0.5) << " dpi" << endl;
#endif

  if (flags & font::FONT_IN_USE)
    return;

  flags |= font::FONT_IN_USE;

  // For virtual fonts, also go through the list of referred fonts
  if (flags & font::FONT_VIRTUAL) {
    QIntDictIterator<font> it(vf_table);
    while( it.current() ) {
      it.current()->flags |= font::FONT_IN_USE;
      ++it;
    }
  }
}

/** Returns a pointer to glyph number ch in the font, or NULL, if this
    number does not exist. This function first reads the bitmap of the
    character from the PK-file, if necessary */

struct glyph *font::glyphptr(unsigned int ch) {
  
  struct glyph *g = glyphtable+ch;
  if (g->bitmap.bits == NULL) {
    if (g->addr == 0) {
      kdError() << i18n("Character %1 not defined in font %2").arg(ch).arg(fontname) << endl;
      g->addr = -1;
      return NULL;
    }
    if (g->addr == -1)
      return NULL;	/* previously flagged missing char */

    if (file == NULL) {
      file = xfopen(filename.latin1(), OPEN_MODE);
      if (file == NULL) {
	oops(QString(i18n("Font file disappeared: %1")).arg(filename) );
	return NULL;
      }
    }
    Fseek(file, g->addr, 0);
    read_PK_char(ch);
    timestamp = ++current_timestamp;

    if (g->bitmap.bits == NULL) {
      g->addr = -1;
      return NULL;
    }
  }

  return g;
}
