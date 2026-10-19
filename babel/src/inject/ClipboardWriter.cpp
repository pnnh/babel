#include "babel/src/inject/ClipboardWriter.h"

#include <QClipboard>
#include <QGuiApplication>

namespace babel::inject {

void ClipboardWriter::write(const QString &text) {
  if (QClipboard *clipboard = QGuiApplication::clipboard()) {
    clipboard->setText(text);
  }
}

}  // namespace babel::inject
