/*
 *   Copyright (C) 2006 Aaron Seigo <aseigo@kde.org>
 *   Copyright (C) 2016 Kai Uwe Broulik <kde@privat.broulik.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License version 2 as
 *   published by the Free Software Foundation
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "shellrunner.h"

#include <KAuthorized>
#include <KLocalizedString>
#include <KNotificationJobUiDelegate>
#include <KToolInvocation>
#include <KShell>
#include <QRegularExpression>
#include <QStandardPaths>

#include <KIO/CommandLauncherJob>
#include <QProcess>
#include <QFileInfo>

K_EXPORT_PLASMA_RUNNER_WITH_JSON(ShellRunner, "plasma-runner-shell.json")

ShellRunner::ShellRunner(QObject *parent, const QVariantList &args)
    : Plasma::AbstractRunner(parent, args)
{
    setObjectName(QStringLiteral("Command"));
    setPriority(AbstractRunner::HighestPriority);
    // If the runner is not authorized we can suspend it
    bool enabled = KAuthorized::authorize(QStringLiteral("run_command")) && KAuthorized::authorize(QStringLiteral("shell_access"));
    suspendMatching(!enabled);

    addSyntax(Plasma::RunnerSyntax(QStringLiteral(":q:"), i18n("Finds commands that match :q:, using common shell syntax")));
    m_actionList = {addAction(QStringLiteral("runInTerminal"),
                            QIcon::fromTheme(QStringLiteral("utilities-terminal")),
                            i18n("Run in Terminal Window"))};
    m_matchIcon = QIcon::fromTheme(QStringLiteral("system-run"));
    // We only want to read the bash aliases/functions if the configured shell is bash
    if (QFileInfo(qEnvironmentVariable("SHELL")).fileName() == QLatin1String("bash")) {
        parseBashAliasesAndFunctions();
    }
}

ShellRunner::~ShellRunner()
{
}

void ShellRunner::match(Plasma::RunnerContext &context)
{
    bool isShellCommand = context.type() == Plasma::RunnerContext::ShellCommand;
    QStringList envs;
    QString command = context.query();
    // If it is not a shell command we check if we use ENV variables, FEATURE: 409107
    // This is not recognized when setting the context type and we can't change it, because
    // other runners depend on the current pattern
    if (!isShellCommand) {
        isShellCommand = parseENVVariables(context.query(), envs, command);
    }
    // If it does not contain ENV variables it might be intended as a bash alias/function
    if (!isShellCommand) {
        if (m_bashCompatibleStrings.contains(KShell::splitArgs(context.query()).first())) {
            isShellCommand = true;
            // We want to launch bash in interactive mode and execute the command in it
            command = QStringLiteral("bash -i -c %1").arg(KShell::quoteArg(command));
        }
    }
    if (isShellCommand) {
        const QString term = context.query();
        Plasma::QueryMatch match(this);
        match.setId(term);
        match.setType(Plasma::QueryMatch::ExactMatch);
        match.setIcon(m_matchIcon);
        match.setText(i18n("Run %1", term));
        match.setData(QVariantList({command, envs}));
        match.setRelevance(0.7);
        context.addMatch(match);
    }
}

void ShellRunner::run(const Plasma::RunnerContext &context, const Plasma::QueryMatch &match)
{
    Q_UNUSED(context)
    const QVariantList data = match.data().toList();
    if (match.selectedAction()) {
        KToolInvocation::invokeTerminal(data.at(0).toString(), data.at(1).toStringList());
        return;
    }

    auto *job = new KIO::CommandLauncherJob(data.at(0).toString());
    job->setUiDelegate(new KNotificationJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled));
    job->start();
}

QList<QAction *> ShellRunner::actionsForMatch(const Plasma::QueryMatch &match)
{
    Q_UNUSED(match)

    return m_actionList;
}

bool ShellRunner::parseENVVariables(const QString &query, QStringList &envs, QString &command)
{
    const static QRegularExpression envRegex = QRegularExpression(QStringLiteral("^.+=.+$"));
    const QStringList split = KShell::splitArgs(query);
    for (const auto &entry : split) {
        if (!QStandardPaths::findExecutable(entry).isEmpty()) {
            command = KShell::joinArgs(split.mid(split.indexOf(entry)));
            return true;
        } else if (envRegex.match(entry).hasMatch()) {
            envs.append(entry);
        } else {
            return false;
        }
    }
    return false;
}

void ShellRunner::parseBashAliasesAndFunctions()
{
    // Bash aliases
    QProcess aliasesProcess;
    aliasesProcess.start(QStringLiteral("bash"), {"-c", "-i", "alias"});
    aliasesProcess.waitForFinished(250);
    const QStringList aliasOutputLines = QString(aliasesProcess.readAll()).split('\n');
    for (const auto &alias : aliasOutputLines) {
        QString prefixRemoved = QString(alias).remove(0, strlen("alias "));
        m_bashCompatibleStrings << prefixRemoved.left(prefixRemoved.indexOf('='));
    }

    // Bash functions
    QProcess functionProcess;
    functionProcess.start(QStringLiteral("bash"), {"-c", "-i", "declare -F"});
    functionProcess.waitForFinished(250);
    const QStringList functionOutputLines = QString(functionProcess.readAll()).split('\n');
    for (const auto &function : functionOutputLines) {
        m_bashCompatibleStrings << QString(function).remove(0, strlen("declare -f "));
    }
}

#include "shellrunner.moc"
