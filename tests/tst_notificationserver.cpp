/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    The freedesktop front door: what handleNotify() does with what an
    application sends.

    Any program in the session can call Notify with anything it likes, so this
    is where hostile input arrives. Most of these tests come from a bug hunt
    that fed the daemon malformed notifications under AddressSanitizer: one
    image header crashed it, a few thousand actions froze it, and an id from
    the portal's range let one sender overwrite another's notification.

    Nothing here opens a bus. handleNotify() is the call the adaptor makes, so
    calling it directly exercises the same path.
*/
#include "historymodel.h"
#include "iconprovider.h"
#include "notificationmodel.h"
#include "notificationserver.h"
#include "notifyimageprovider.h"
#include "testenv.h"

#include <QDBusMetaType>
#include <QImage>
#include <QTemporaryDir>

namespace
{
/* An image-data hint. Off the bus it is still a QDBusArgument; a
   QDBusArgument cannot be built for reading without a bus, so the test hands
   over the struct and the server reads both forms through qdbus_cast. */
QVariant imageHint(const ImageData &img)
{
    return QVariant::fromValue(img);
}

ImageData solid(int w, int h, int channels, QColor colour)
{
    ImageData img;
    img.width = w;
    img.height = h;
    img.channels = channels;
    img.bitsPerSample = 8;
    img.hasAlpha = channels == 4;
    img.rowstride = w * channels;
    for (int i = 0; i < w * h; ++i) {
        img.data.append(char(colour.red()));
        img.data.append(char(colour.green()));
        img.data.append(char(colour.blue()));
        if (channels == 4) {
            img.data.append(char(colour.alpha()));
        }
    }
    return img;
}
} // namespace

class TestNotificationServer : public QObject
{
    Q_OBJECT

private:
    struct Fixture {
        NotificationModel model;
        HistoryModel history;
        NotificationServer server{&model, &history};

        Fixture()
        {
            model.setCoalesce(0, 0);
            model.setTimeouts(0, 0, 0);
        }

        uint notify(const QString &summary,
                    const QVariantMap &hints = {},
                    const QString &appIcon = {},
                    const QStringList &actions = {},
                    uint replaces = 0,
                    const QString &body = QStringLiteral("body"))
        {
            return server.handleNotify(QStringLiteral("Test"), replaces, appIcon, summary, body,
                                       actions, hints, -1);
        }

        int rowOf(uint id) const
        {
            for (int r = 0; r < model.rowCount(); ++r) {
                if (model.index(r).data(NotificationModel::IdRole).toUInt() == id) {
                    return r;
                }
            }
            return -1;
        }
        QVariant role(uint id, int r) const { return model.index(rowOf(id)).data(r); }
        QString icon(uint id) const { return role(id, NotificationModel::IconSourceRole).toString(); }
    };

private Q_SLOTS:
    void initTestCase()
    {
        qDBusRegisterMetaType<ImageData>();
    }

    // ---- image-data -------------------------------------------------------

    /* The crash. 65536 * 65536 is 2^32, which wraps an int to zero, so the
       length check thought these 64 bytes were enough for a 1x65537 image.
       QImage::copy then read four gigabytes past the end of the buffer.
       Any application could send it. */
    void overflowingImageHeaderIsRejected()
    {
        ImageData evil;
        evil.width = 1;
        evil.height = 65537;
        evil.rowstride = 65536;
        evil.hasAlpha = false;
        evil.bitsPerSample = 8;
        evil.channels = 3;
        evil.data = QByteArray(64, 'x');

        QVERIFY(!evil.isValid());

        Fixture f;
        const uint id = f.notify(QStringLiteral("evil"), {{QStringLiteral("image-data"), imageHint(evil)}});
        QVERIFY(f.rowOf(id) >= 0);
        QVERIFY(!f.icon(id).startsWith(QLatin1String("image://notifyimage/")));
    }

    /* Each of these describes more pixels than it carries, or a layout the
       renderer cannot draw. None may reach the image provider. */
    void malformedImagesAreRejected_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<int>("rowstride");
        QTest::addColumn<int>("bps");
        QTest::addColumn<int>("channels");
        QTest::addColumn<int>("bytes");

        QTest::newRow("rowstride shorter than a row") << 4 << 4 << 4 << 8 << 4 << 64;
        QTest::newRow("negative rowstride") << 4 << 4 << -16 << 8 << 4 << 64;
        QTest::newRow("two channels") << 4 << 4 << 8 << 8 << 2 << 32;
        QTest::newRow("sixteen bits per sample") << 4 << 4 << 32 << 16 << 4 << 128;
        QTest::newRow("too few bytes") << 4 << 4 << 16 << 8 << 4 << 63;
        QTest::newRow("absurd width") << 100000 << 1 << 400000 << 8 << 4 << 64;
        QTest::newRow("wrapping width") << 0x40000001 << 1 << 4 << 8 << 4 << 64;
    }
    void malformedImagesAreRejected()
    {
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(int, rowstride);
        QFETCH(int, bps);
        QFETCH(int, channels);
        QFETCH(int, bytes);

        ImageData img;
        img.width = width;
        img.height = height;
        img.rowstride = rowstride;
        img.bitsPerSample = bps;
        img.channels = channels;
        img.data = QByteArray(bytes, '\x7f');
        QVERIFY(!img.isValid());

        Fixture f;
        const uint id = f.notify(QStringLiteral("bad"), {{QStringLiteral("image-data"), imageHint(img)}});
        QVERIFY(!f.icon(id).startsWith(QLatin1String("image://notifyimage/")));
    }

    /* The fix must not throw away good images: RGB and RGBA, padded rows
       included, still render, with the right pixels. */
    void wellFormedImagesStillRender_data()
    {
        QTest::addColumn<int>("channels");
        QTest::addColumn<int>("padding");
        QTest::newRow("rgba") << 4 << 0;
        QTest::newRow("rgb") << 3 << 0;
        QTest::newRow("rgb, padded rows") << 3 << 3;
    }
    void wellFormedImagesStillRender()
    {
        QFETCH(int, channels);
        QFETCH(int, padding);

        ImageData img = solid(3, 2, channels, QColor(200, 10, 20));
        if (padding) {
            QByteArray padded;
            for (int row = 0; row < img.height; ++row) {
                padded += img.data.mid(qsizetype(row) * img.rowstride, img.rowstride);
                if (row < img.height - 1) {
                    padded += QByteArray(padding, '\0');   // no padding needed after the last row
                }
            }
            img.rowstride += padding;
            img.data = padded;
        }
        QVERIFY(img.isValid());

        Fixture f;
        const uint id = f.notify(QStringLiteral("ok"), {{QStringLiteral("image-data"), imageHint(img)}});
        QCOMPARE(f.icon(id), QStringLiteral("image://notifyimage/%1").arg(id));

        NotifyImageProvider provider(&f.model, &f.history);
        QSize size;
        const QImage out = provider.requestImage(QString::number(id), &size, {});
        QCOMPARE(out.size(), QSize(3, 2));
        QCOMPARE(QColor(out.pixel(2, 1)).red(), 200);
        QCOMPARE(QColor(out.pixel(2, 1)).blue(), 20);
    }

    /* Images are drawn at icon size. Keeping a full-resolution copy of every
       one in the model and in history is how a few large images become a
       gigabyte, so they are scaled down when they arrive. */
    void largeImagesAreScaledDownOnArrival()
    {
        const ImageData big = solid(1600, 800, 4, Qt::blue);
        QVERIFY(big.isValid());

        Fixture f;
        const uint id = f.notify(QStringLiteral("big"), {{QStringLiteral("image-data"), imageHint(big)}});
        const ImageData stored = f.model.imageFor(id);
        QVERIFY(stored.isValid());
        QVERIFY(stored.width <= ImageData::maxStoredEdge);
        QVERIFY(stored.height <= ImageData::maxStoredEdge);
        QCOMPARE(stored.width, 2 * stored.height);   // aspect kept
    }

    // ---- size limits -------------------------------------------------------

    /* Two thousand actions froze the daemon for eight seconds while QML built
       a button for each. Nobody needs more than a handful. */
    void actionsAreCapped()
    {
        QStringList actions;
        for (int i = 0; i < 2000; ++i) {
            actions << QStringLiteral("a%1").arg(i) << QStringLiteral("Action %1").arg(i);
        }
        Fixture f;
        const uint id = f.notify(QStringLiteral("many"), {}, {}, actions);
        const QVariantList drawn = f.role(id, NotificationModel::ActionsRole).toList();
        QVERIFY(!drawn.isEmpty());
        QVERIFY2(drawn.size() <= NotificationServer::maxActions,
                 qPrintable(QString::number(drawn.size())));
        /* The first ones are the ones kept, in order. */
        QCOMPARE(drawn.first().toMap().value(QStringLiteral("key")).toString(), QStringLiteral("a0"));
    }

    /* "default" and the reply field are not drawn as buttons, so they must
       not use up the allowance either. */
    void cappingKeepsTheDefaultAction()
    {
        QStringList actions;
        for (int i = 0; i < 50; ++i) {
            actions << QStringLiteral("a%1").arg(i) << QStringLiteral("Action %1").arg(i);
        }
        actions << QStringLiteral("default") << QString();
        Fixture f;
        const uint id = f.notify(QStringLiteral("many"), {}, {}, actions);
        QVERIFY(f.role(id, NotificationModel::HasDefaultActionRole).toBool());
    }

    /* An odd-length list has a key with no label. It is dropped rather than
       pairing every later key with the wrong label. */
    void unpairedTrailingActionIsDropped()
    {
        Fixture f;
        const uint id = f.notify(QStringLiteral("odd"), {}, {},
                                 {QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("b")});
        const QVariantList drawn = f.role(id, NotificationModel::ActionsRole).toList();
        QCOMPARE(drawn.size(), 1);
    }

    void summaryAndBodyAreCapped()
    {
        Fixture f;
        const QString huge(1000000, QLatin1Char('x'));
        const uint id = f.notify(huge, {}, {}, {}, 0, huge);
        QVERIFY(f.role(id, NotificationModel::SummaryRole).toString().size() <= NotificationServer::maxSummary);
        QVERIFY(f.role(id, NotificationModel::BodyRole).toString().size() <= NotificationServer::maxBody);
    }

    // ---- ids ---------------------------------------------------------------

    /* Ids with the top bit set belong to portal notifications. A freedesktop
       sender naming one in replaces_id was overwriting a Flatpak's
       notification — any app could replace any sandboxed app's message. */
    void portalIdsCannotBeReplacedFromTheFreedesktopSide()
    {
        Fixture f;
        Notification portal;
        portal.id = NotificationId::portalMask | 1;
        portal.appName = QStringLiteral("Sandboxed");
        portal.summary = QStringLiteral("genuine");
        f.server.deliver(portal);
        QVERIFY(f.rowOf(portal.id) >= 0);

        const uint id = f.notify(QStringLiteral("forged"), {}, {}, {}, portal.id);
        QVERIFY(!NotificationId::isPortal(id));
        QCOMPARE(f.role(portal.id, NotificationModel::SummaryRole).toString(), QStringLiteral("genuine"));
    }

    // ---- icons -------------------------------------------------------------

    /* The spec allows app_icon and image-path to be file:// URIs, and
       Chromium-based apps send them. The icon provider is given plain paths. */
    void fileUrisBecomePaths()
    {
        Fixture f;
        const uint a = f.notify(QStringLiteral("uri"), {}, QStringLiteral("file:///tmp/some%20icon.png"));
        QCOMPARE(f.icon(a), QStringLiteral("image://icon//tmp/some icon.png"));

        const uint b = f.notify(QStringLiteral("hint"),
                                {{QStringLiteral("image-path"), QStringLiteral("file:///tmp/pic.png")}});
        QCOMPARE(f.icon(b), QStringLiteral("image://icon//tmp/pic.png"));
    }

    /* The spec's order is image-data, then image-path, then app_icon:
       image-path is the picture for this message, app_icon the sender's
       logo. */
    void imagePathOutranksAppIcon()
    {
        Fixture f;
        const uint id = f.notify(QStringLiteral("both"),
                                 {{QStringLiteral("image-path"), QStringLiteral("/tmp/avatar.png")}},
                                 QStringLiteral("org.example.Chat"));
        QCOMPARE(f.icon(id), QStringLiteral("image://icon//tmp/avatar.png"));
    }

    /* A path has to load as a file. QIcon::fromTheme() only knows names:
       given a path, Qt's own loader finds nothing, and the notification
       showed a generic icon or none. */
    void iconProviderLoadsFilePaths()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("avatar.png"));
        QImage img(32, 32, QImage::Format_ARGB32);
        img.fill(QColor(10, 200, 30));
        QVERIFY(img.save(path));

        IconProvider provider;
        for (const QString &id : {path, QUrl::fromLocalFile(path).toString()}) {
            QSize size;
            const QPixmap pm = provider.requestPixmap(id, &size, QSize(32, 32));
            QVERIFY2(!pm.isNull(), qPrintable(id));
            QCOMPARE(QColor(pm.toImage().pixel(16, 16)).green(), 200);
        }
    }
};

GLASSOSD_TEST_MAIN(TestNotificationServer)
#include "tst_notificationserver.moc"
