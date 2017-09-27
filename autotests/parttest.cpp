/***************************************************************************
 *   Copyright (C) 2013 by Albert Astals Cid <aacid@kde.org>               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include <QtTest>

#include "../core/annotations.h"
#include "../core/page.h"
#include "../part.h"
#include "../ui/toc.h"
#include "../ui/pageview.h"

#include <KConfigDialog>

#include <QClipboard>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTreeView>

namespace Okular
{
class PartTest
    : public QObject
{
    Q_OBJECT

        static bool openDocument(Okular::Part *part, const QString &filePath);

    private slots:
        void testReload();
        void testCanceledReload();
        void testTOCReload();
        void testForwardPDF();
        void testForwardPDF_data();
        void testGeneratorPreferences();
        void testSelectText();
        void testClickInternalLink();
        void testSaveAs();
        void testSaveAs_data();
};

class PartThatHijacksQueryClose : public Okular::Part
{
    public:
        PartThatHijacksQueryClose(QWidget* parentWidget, QObject* parent,
                                  const QVariantList& args)
        : Okular::Part(parentWidget, parent, args),
          behavior(PassThru)
        {}

        enum Behavior { PassThru, ReturnTrue, ReturnFalse };

        void setQueryCloseBehavior(Behavior new_behavior)
        {
            behavior = new_behavior;
        }

        bool queryClose() override
        {
             if (behavior == PassThru)
                 return Okular::Part::queryClose();
             else // ReturnTrue or ReturnFalse
                 return (behavior == ReturnTrue);
        }
    private:
        Behavior behavior;
};

bool PartTest::openDocument(Okular::Part *part, const QString &filePath)
{
    part->openDocument( filePath );
    return part->m_document->isOpened();
}

// Test that Okular doesn't crash after a successful reload
void PartTest::testReload()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, nullptr, dummyArgs);
    QVERIFY( openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")) );
    part.reload();
    qApp->processEvents();
}

// Test that Okular doesn't crash after a canceled reload
void PartTest::testCanceledReload()
{
    QVariantList dummyArgs;
    PartThatHijacksQueryClose part(nullptr, nullptr, dummyArgs);
    QVERIFY( openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")) );

    // When queryClose() returns false, the reload operation is canceled (as if
    // the user had chosen Cancel in the "Save changes?" message box)
    part.setQueryCloseBehavior(PartThatHijacksQueryClose::ReturnFalse);

    part.reload();

    qApp->processEvents();
}

void PartTest::testTOCReload()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, nullptr, dummyArgs);
    QVERIFY( openDocument(&part, QStringLiteral(KDESRCDIR "data/tocreload.pdf")) );
    QCOMPARE(part.m_toc->expandedNodes().count(), 0);
    part.m_toc->m_treeView->expandAll();
    QCOMPARE(part.m_toc->expandedNodes().count(), 3);
    part.reload();
    qApp->processEvents();
    QCOMPARE(part.m_toc->expandedNodes().count(), 3);
}

void PartTest::testForwardPDF()
{
    QFETCH(QString, dir);

    QVariantList dummyArgs;
    Okular::Part part(nullptr, nullptr, dummyArgs);

    // Create temp dir named like this: ${system temp dir}/${random string}/${dir}
    const QTemporaryDir tempDir;
    const QDir workDir(QDir(tempDir.path()).filePath(dir));
    workDir.mkpath(QStringLiteral("."));

    QFile f(QStringLiteral(KDESRCDIR "data/synctextest.tex"));
    const QString texDestination = workDir.path() + QStringLiteral("/synctextest.tex");
    QVERIFY(f.copy(texDestination));
    QProcess process;
    process.setWorkingDirectory(workDir.path());
    process.start(QStringLiteral("pdflatex"), QStringList() << QStringLiteral("-synctex=1") << QStringLiteral("-interaction=nonstopmode") << texDestination);
    bool started = process.waitForStarted();
    if (!started) {
        qDebug() << "start error:" << process.error();
        qDebug() << "start stdout:" << process.readAllStandardOutput();
        qDebug() << "start stderr:" << process.readAllStandardError();
    }
    QVERIFY(started);

    process.waitForFinished();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qDebug() << "exit error:" << process.error() << "status" << process.exitStatus() << "code" << process.exitCode();
        qDebug() << "exit stdout:" << process.readAllStandardOutput();
        qDebug() << "exit stderr:" << process.readAllStandardError();
    }

    const QString pdfResult = workDir.path() + QStringLiteral("/synctextest.pdf");
    
    QVERIFY(QFile::exists(pdfResult));
    
    QVERIFY( openDocument(&part, pdfResult) );
    part.m_document->setViewportPage(0);
    QCOMPARE(part.m_document->currentPage(), 0u);
    part.closeUrl();
    
    QUrl u(QUrl::fromLocalFile(pdfResult));
    u.setFragment(QStringLiteral("src:100") + texDestination);
    part.openUrl(u);
    QCOMPARE(part.m_document->currentPage(), 1u);
}

void PartTest::testForwardPDF_data()
{
    QTest::addColumn<QString>("dir");

    QTest::newRow("non-utf8") << QString::fromUtf8("synctextest");
    QTest::newRow("utf8")     << QString::fromUtf8("ßðđđŋßðđŋ");
}

void PartTest::testGeneratorPreferences()
{
    KConfigDialog * dialog;
    QVariantList dummyArgs;
    Okular::Part part(nullptr, nullptr, dummyArgs);

    // Test that we don't crash while opening the dialog
    dialog = part.slotGeneratorPreferences();
    qApp->processEvents();
    delete dialog; // closes the dialog and recursively destroys all widgets

    // Test that we don't crash while opening a new instance of the dialog
    // This catches attempts to reuse widgets that have been destroyed
    dialog = part.slotGeneratorPreferences();
    qApp->processEvents();
    delete dialog;
}

void PartTest::testSelectText()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file2.pdf")));
    part.widget()->show();
    QTest::qWaitForWindowExposed(part.widget());

    const int width = part.m_pageView->horizontalScrollBar()->maximum() +
                      part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() +
                       part.m_pageView->viewport()->height();

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    const int mouseY = height * 0.052;
    const int mouseStartX = width * 0.12;
    const int mouseEndX = width * 0.7;

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(mouseStartX, mouseY));
    QTest::mousePress(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(mouseStartX, mouseY));
    QTest::mouseMove(part.m_pageView->viewport(), QPoint(mouseEndX, mouseY));
    // without this wait the test fails. 100ms were enough on my local system, but when running under valgrind
    // or on the CI server we need to wait longer.
    QTest::qWait(1000);
    QTest::mouseRelease(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(mouseEndX, mouseY));

    QApplication::clipboard()->clear();
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "copyTextSelection"));

    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Hola que tal\n"));
}

void PartTest::testClickInternalLink()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file2.pdf")));
    part.widget()->show();
    QTest::qWaitForWindowExposed(part.widget());

    const int width = part.m_pageView->horizontalScrollBar()->maximum() +
                      part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() +
                       part.m_pageView->viewport()->height();

    part.m_document->setViewportPage(0);

    // wait for pixmap
    while (!part.m_document->page(0)->hasPixmap(part.m_pageView))
        QTest::qWait(100);

    QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseNormal");

    QCOMPARE(part.m_document->currentPage(), 0u);
    QTest::mouseClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(width * 0.17, height * 0.05));
    QCOMPARE(part.m_document->currentPage(), 1u);
}

void PartTest::testSaveAs()
{
    QFETCH(QString, file);
    QFETCH(QString, extension);
    QFETCH(bool, nativelySupportsAnnotations);

    // If we expect not to be able to preserve annotations when we write a
    // native file, disable the warning otherwise the test will wait for the
    // user to confirm. On the other end, if we expect annotations to be
    // preserved (and thus no warning), we keep the warning on so that if it
    // shows the test timeouts and we can notice that something is wrong.
    const Part::SaveAsFlags saveAsNativeFlags = nativelySupportsAnnotations ?
        Part::NoSaveAsFlags : Part::SaveAsDontShowWarning;

    QString annotName;
    QTemporaryFile archiveSave( QString( "%1/okrXXXXXX.okular" ).arg( QDir::tempPath() ) );
    QTemporaryFile nativeDirectSave( QString( "%1/okrXXXXXX.%2" ).arg( QDir::tempPath() ).arg ( extension ) );
    QTemporaryFile nativeFromArchiveFile( QString( "%1/okrXXXXXX.%2" ).arg( QDir::tempPath() ).arg ( extension ) );
    QVERIFY( archiveSave.open() ); archiveSave.close();
    QVERIFY( nativeDirectSave.open() ); nativeDirectSave.close();
    QVERIFY( nativeFromArchiveFile.open() ); nativeFromArchiveFile.close();

    qDebug() << "Open file, add annotation and save both natively and to .okular";
    {
        Okular::Part part(nullptr, nullptr, QVariantList());
        part.openDocument( file );

        Okular::Annotation *annot = new Okular::TextAnnotation();
        annot->setBoundingRectangle( Okular::NormalizedRect( 0.1, 0.1, 0.15, 0.15 ) );
        annot->setContents( "annot contents" );
        part.m_document->addPageAnnotation( 0, annot );
        annotName = annot->uniqueName();

        QVERIFY( part.saveAs( QUrl::fromLocalFile( archiveSave.fileName() ), Part::SaveAsOkularArchive ) );
        QVERIFY( part.saveAs( QUrl::fromLocalFile( nativeDirectSave.fileName() ), saveAsNativeFlags ) );

        part.closeUrl();
    }

    qDebug() << "Open the .okular, check that the annotation is present and save to native";
    {
        Okular::Part part(nullptr, nullptr, QVariantList());
        part.openDocument( archiveSave.fileName() );

        QCOMPARE( part.m_document->page( 0 )->annotations().size(), 1 );
        QCOMPARE( part.m_document->page( 0 )->annotations().first()->uniqueName(), annotName );

        QVERIFY( part.saveAs( QUrl::fromLocalFile( nativeFromArchiveFile.fileName() ), saveAsNativeFlags ) );

        part.closeUrl();
    }

    qDebug() << "Open the native file saved directly, and check that the annot"
        << "is there iff we expect it";
    {
        Okular::Part part(nullptr, nullptr, QVariantList());
        part.openDocument( nativeDirectSave.fileName() );

        QCOMPARE( part.m_document->page( 0 )->annotations().size(), nativelySupportsAnnotations ? 1 : 0 );
        if ( nativelySupportsAnnotations )
            QCOMPARE( part.m_document->page( 0 )->annotations().first()->uniqueName(), annotName );

        part.closeUrl();
    }

    qDebug() << "Open the native file saved from the .okular, and check that the annot"
        << "is there iff we expect it";
    {
        Okular::Part part(nullptr, nullptr, QVariantList());
        part.openDocument( nativeFromArchiveFile.fileName() );

        QCOMPARE( part.m_document->page( 0 )->annotations().size(), nativelySupportsAnnotations ? 1 : 0 );
        if ( nativelySupportsAnnotations )
            QCOMPARE( part.m_document->page( 0 )->annotations().first()->uniqueName(), annotName );

        part.closeUrl();
    }
}

void PartTest::testSaveAs_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("extension");
    QTest::addColumn<bool>("nativelySupportsAnnotations");

    QTest::newRow("pdf") << KDESRCDIR "data/file1.pdf" << "pdf" << true;
    QTest::newRow("epub") << KDESRCDIR "data/contents.epub" << "epub" << false;
}

}

int main(int argc, char *argv[])
{
    // Force consistent locale
    QLocale locale(QStringLiteral("en_US.UTF-8"));
    if (locale == QLocale::c()) { // This is the way to check if the above worked
        locale = QLocale(QLocale::English, QLocale::UnitedStates);
    }

    QLocale::setDefault(locale);
    qputenv("LC_ALL", "en_US.UTF-8"); // For UNIX, third-party libraries

    // Ensure consistent configs/caches
    QTemporaryDir homeDir; // QTemporaryDir automatically cleans up when it goes out of scope
    Q_ASSERT(homeDir.isValid());
    QByteArray homePath = QFile::encodeName(homeDir.path());
    qDebug() << homePath;
    qputenv("USERPROFILE", homePath);
    qputenv("HOME", homePath);
    qputenv("XDG_DATA_HOME", homePath + "/.local");
    qputenv("XDG_CONFIG_HOME", homePath + "/.kde-unit-test/xdg/config");

    // Disable fancy debug output
    qunsetenv("QT_MESSAGE_PATTERN");

    QApplication app( argc, argv );
    app.setApplicationName(QLatin1String("okularparttest"));
    app.setOrganizationDomain(QLatin1String("kde.org"));
    app.setQuitOnLastWindowClosed(false);

    qRegisterMetaType<QUrl>(); /*as done by kapplication*/
    qRegisterMetaType<QList<QUrl>>();

    Okular::PartTest test;

    return QTest::qExec( &test, argc, argv );
}

#include "parttest.moc"
