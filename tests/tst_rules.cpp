/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    The rules engine.

    Worth testing first because it is the part of glassosd a user configures
    by hand and cannot debug: a rule that silently fails to match looks
    identical to a notification that was never sent. The dunst-compatibility
    claims in particular are only true if they keep being true — globs rather
    than regexes, every set matcher having to match, and later rules refining
    earlier ones instead of the first match winning.
*/
#include "rules.h"
#include "testenv.h"

#include <KSharedConfig>
#include <QFile>
#include <QStandardPaths>

class TestRules : public QObject
{
    Q_OBJECT

private:
    /* Rules only ever reads config, so the whole fixture is a string. */
    static Rules loaded(const QString &ini, const QString &focus = {})
    {
        const QString path = QStandardPaths::writableLocation(
                                 QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/glassosdrc");
        QFile f(path);
        /* qFatal, not Q_ASSERT: assertions compile away in a release build,
           and this helper would then silently write nothing and hand every
           test an empty rule set — which reads as "no rule matched", the
           exact failure the suite exists to detect. */
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qFatal("cannot write %s", qPrintable(path));
        }
        f.write(ini.toUtf8());
        f.close();

        auto config = KSharedConfig::openConfig(QStringLiteral("glassosdrc"));
        config->reparseConfiguration();

        Rules r;
        r.setActiveFocus(focus);
        r.load(config);
        return r;
    }

    static Notification note(const QString &app, const QString &summary = {},
                             const QString &body = {})
    {
        Notification n;
        n.id = 1;
        n.appName = app;
        n.summary = summary;
        n.body = body;
        return n;
    }

private Q_SLOTS:
    /* The dunstrc line this project was built to keep working. */
    void globNotRegex()
    {
        const Rules r = loaded(QStringLiteral(
            "[Rule pen]\n"
            "appname=Battery Monitor\n"
            "summary=Device Battery Low*\n"
            "set_stack_tag=pen-battery\n"));

        Notification n = note(QStringLiteral("Battery Monitor"),
                              QStringLiteral("Device Battery Low (6% Remaining)"));
        r.apply(n);
        QCOMPARE(n.stackTag, QStringLiteral("pen-battery"));

        /* A glob is anchored at both ends; a regex would have matched here on
           a substring, so this is what distinguishes the two. */
        Notification other = note(QStringLiteral("Battery Monitor"),
                                  QStringLiteral("Warning: Device Battery Low"));
        r.apply(other);
        QVERIFY(other.stackTag.isEmpty());
    }

    void matchingIsCaseInsensitive()
    {
        const Rules r = loaded(QStringLiteral(
            "[Rule x]\n"
            "appname=discord\n"
            "set_urgency=0\n"));
        Notification n = note(QStringLiteral("Discord"));
        r.apply(n);
        QCOMPARE(int(n.urgency), 0);
    }

    /* Every matcher that is set has to match — dunst's AND, not an OR. */
    void allMatchersMustMatch()
    {
        const Rules r = loaded(QStringLiteral(
            "[Rule x]\n"
            "appname=Thunderbird\n"
            "summary=Meeting*\n"
            "set_stack_tag=cal\n"));

        Notification hit = note(QStringLiteral("Thunderbird"), QStringLiteral("Meeting at 3"));
        r.apply(hit);
        QCOMPARE(hit.stackTag, QStringLiteral("cal"));

        Notification wrongSummary = note(QStringLiteral("Thunderbird"), QStringLiteral("New mail"));
        r.apply(wrongSummary);
        QVERIFY(wrongSummary.stackTag.isEmpty());

        Notification wrongApp = note(QStringLiteral("Evolution"), QStringLiteral("Meeting at 3"));
        r.apply(wrongApp);
        QVERIFY(wrongApp.stackTag.isEmpty());
    }

    void matchUrgencyIsExact()
    {
        const Rules r = loaded(QStringLiteral(
            "[Rule x]\n"
            "match_urgency=2\n"
            "sound=dialog-error\n"));

        Notification critical = note(QStringLiteral("App"));
        critical.urgency = Urgency::Critical;
        r.apply(critical);
        QCOMPARE(critical.sound, QStringLiteral("dialog-error"));

        Notification normal = note(QStringLiteral("App"));
        r.apply(normal);
        QVERIFY(normal.sound.isEmpty());
    }

    /* A rule with no matchers at all applies to everything, which is what
       makes a catch-all possible. */
    void unmatchedFieldsAreWildcards()
    {
        const Rules r = loaded(QStringLiteral(
            "[Rule all]\n"
            "timeout=1000\n"));
        Notification n = note(QStringLiteral("Anything"));
        r.apply(n);
        QCOMPARE(n.timeoutMs, 1000);
    }

    /* Later rules refine earlier ones rather than the first match winning.

       Rules are ordered by name, not by position in the file: KConfig hands
       its groups back as an unordered set, so the file's order is not
       something we can be told. This test is what pins that down — without a
       sort, which of the two rules below won was decided by hash placement. */
    void laterRulesRefineEarlier()
    {
        const Rules r = loaded(QStringLiteral(
            "[Rule a-broad]\n"
            "appname=Slack\n"
            "set_urgency=0\n"
            "timeout=2000\n"
            "[Rule b-narrow]\n"
            "appname=Slack\n"
            "summary=*mentioned you*\n"
            "set_urgency=2\n"));

        Notification mention = note(QStringLiteral("Slack"),
                                    QStringLiteral("alex mentioned you in #dev"));
        r.apply(mention);
        QCOMPARE(int(mention.urgency), 2);
        QCOMPARE(mention.timeoutMs, 2000);   // the broad rule still had its say

        Notification ordinary = note(QStringLiteral("Slack"), QStringLiteral("new message"));
        r.apply(ordinary);
        QCOMPARE(int(ordinary.urgency), 0);
    }

    /* skip_display is tri-state on purpose: without a "false" there is no way
       to write an allow-list, because nothing could undo a catch-all hide. */
    void skipDisplayCanBeTurnedBackOn()
    {
        const Rules r = loaded(QStringLiteral(
            "[Rule a-hide-all]\n"
            "skip_display=true\n"
            "[Rule b-except-signal]\n"
            "appname=Signal\n"
            "skip_display=false\n"));

        Notification signalMsg = note(QStringLiteral("Signal"));
        r.apply(signalMsg);
        QVERIFY(!signalMsg.skipDisplay);

        Notification other = note(QStringLiteral("Steam"));
        r.apply(other);
        QVERIFY(other.skipDisplay);
    }

    /* An absent skip_display must leave an earlier rule's decision alone —
       the difference between "unset" and "false". */
    void absentSkipDisplayLeavesItAlone()
    {
        const Rules r = loaded(QStringLiteral(
            "[Rule a-hide]\n"
            "appname=Steam\n"
            "skip_display=true\n"
            "[Rule b-tag]\n"
            "appname=Steam\n"
            "set_stack_tag=steam\n"));

        Notification n = note(QStringLiteral("Steam"));
        r.apply(n);
        QVERIFY(n.skipDisplay);
        QCOMPARE(n.stackTag, QStringLiteral("steam"));
    }

    void repeatWindowAndOtherActions()
    {
        const Rules r = loaded(QStringLiteral(
            "[Rule pen]\n"
            "appname=Battery Monitor\n"
            "repeat_window=300\n"
            "history_ignore=true\n"
            "snooze=15\n"
            "sound=none\n"
            "run=/usr/bin/true\n"));

        Notification n = note(QStringLiteral("Battery Monitor"));
        QCOMPARE(n.repeatWindowSec, -1);   // the off value, before any rule
        r.apply(n);
        QCOMPARE(n.repeatWindowSec, 300);
        QVERIFY(n.historyIgnore);
        QCOMPARE(n.snoozeMinutes, 15);
        QCOMPARE(n.sound, QStringLiteral("none"));
        QCOMPARE(n.run, QStringLiteral("/usr/bin/true"));
    }

    /* A rule scoped to a focus mode is simply not there when that mode is
       off. This is what makes a mode a bundle rather than a set of edits
       somebody has to remember to undo. */
    void focusScopedRuleOnlyAppliesInThatMode()
    {
        const QString ini = QStringLiteral(
            "[Rule quiet-slack]\n"
            "appname=Slack\n"
            "focus=work\n"
            "skip_display=true\n");

        Notification off = note(QStringLiteral("Slack"));
        loaded(ini).apply(off);
        QVERIFY(!off.skipDisplay);

        Notification on = note(QStringLiteral("Slack"));
        loaded(ini, QStringLiteral("work")).apply(on);
        QVERIFY(on.skipDisplay);
    }

    /* The allow-list is the interesting half: hide everything, then put the
       named few back. */
    void focusAllowListHidesEverythingElse()
    {
        const QString ini = QStringLiteral(
            "[Focus work]\n"
            "Allow=Signal,Thunderbird\n");

        Notification allowed = note(QStringLiteral("Signal"));
        loaded(ini, QStringLiteral("work")).apply(allowed);
        QVERIFY(!allowed.skipDisplay);

        Notification blocked = note(QStringLiteral("Steam"));
        loaded(ini, QStringLiteral("work")).apply(blocked);
        QVERIFY(blocked.skipDisplay);

        /* Nothing at all happens while the mode is off. */
        Notification modeOff = note(QStringLiteral("Steam"));
        loaded(ini).apply(modeOff);
        QVERIFY(!modeOff.skipDisplay);
    }

    /* Allow is matched on either name, because which one a person knows is a
       coin toss — "Signal" is the app name, "signal-desktop" the entry. */
    void focusAllowMatchesDesktopEntryToo()
    {
        Notification n = note(QStringLiteral("Signal Desktop"));
        n.desktopEntry = QStringLiteral("signal-desktop");

        loaded(QStringLiteral("[Focus work]\nAllow=signal-desktop\n"),
               QStringLiteral("work")).apply(n);
        QVERIFY(!n.skipDisplay);
    }

    void focusBlockList()
    {
        const QString ini = QStringLiteral("[Focus focus]\nBlock=Steam\n");

        Notification blocked = note(QStringLiteral("Steam"));
        loaded(ini, QStringLiteral("focus")).apply(blocked);
        QVERIFY(blocked.skipDisplay);

        /* Block alone must not hide anything it did not name. */
        Notification other = note(QStringLiteral("Signal"));
        loaded(ini, QStringLiteral("focus")).apply(other);
        QVERIFY(!other.skipDisplay);
    }

    /* A focus mode is a deliberate act, so its compiled rules run after the
       hand-written ones and get the final say. */
    void focusOutranksHandWrittenRules()
    {
        Notification n = note(QStringLiteral("Steam"));
        loaded(QStringLiteral(
                   "[Rule show-steam]\n"
                   "appname=Steam\n"
                   "skip_display=false\n"
                   "[Focus focus]\n"
                   "Block=Steam\n"),
               QStringLiteral("focus")).apply(n);
        QVERIFY(n.skipDisplay);
    }

    /* The ordering has to be the *name* order and nothing else, so that
       naming rules 10-, 20-, 30- works the way it does everywhere else. Here
       the file order and the name order disagree, and the name order must
       win: the later-named rule sets the timeout that survives. */
    void rulesAreOrderedByNameNotByFilePosition()
    {
        /* Six of them, written in an order that is neither the name order nor
           its reverse. One or two rules could pass on luck; six agreeing on
           the last name alphabetically could not. */
        const Rules r = loaded(QStringLiteral(
            "[Rule 30-c]\nappname=App\ntimeout=3000\n"
            "[Rule 60-f]\nappname=App\ntimeout=6000\n"
            "[Rule 10-a]\nappname=App\ntimeout=1000\n"
            "[Rule 40-d]\nappname=App\ntimeout=4000\n"
            "[Rule 20-b]\nappname=App\ntimeout=2000\n"
            "[Rule 50-e]\nappname=App\ntimeout=5000\n"));
        QCOMPARE(r.count(), 6);

        Notification n = note(QStringLiteral("App"));
        r.apply(n);
        QCOMPARE(n.timeoutMs, 6000);
    }

    /* Same input, same answer, every time. Reading the same file twice used
       to be able to produce two different rule orders. */
    void loadingIsDeterministic()
    {
        const QString ini = QStringLiteral(
            "[Rule zulu]\n"
            "appname=App\n"
            "set_urgency=0\n"
            "[Rule alpha]\n"
            "appname=App\n"
            "set_urgency=2\n"
            "[Rule mike]\n"
            "appname=App\n"
            "timeout=5000\n");

        for (int i = 0; i < 5; ++i) {
            Notification n = note(QStringLiteral("App"));
            loaded(ini).apply(n);
            QCOMPARE(int(n.urgency), 0);   // zulu sorts last, so zulu wins
            QCOMPARE(n.timeoutMs, 5000);
        }
    }

    void nonRuleGroupsAreIgnored()
    {
        const Rules r = loaded(QStringLiteral(
            "[Notifications]\n"
            "appname=Slack\n"
            "[Appearance]\n"
            "Theme=frosted\n"));
        QCOMPARE(r.count(), 0);
    }
};

GLASSOSD_TEST_MAIN(TestRules)
#include "tst_rules.moc"
