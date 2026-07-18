#!/usr/bin/env bash
# Guest: avoid QUrl::scheme() in QQmlImportDatabase path helpers (CR2=0x9 during QQmlEngine ctor).
set -eu
python3 - <<'PY'
from pathlib import Path

path = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmlimport.cpp")
text = path.read_text()

add_import_old = """void QQmlImportDatabase::addImportPath(const QString& path)
{
    qCDebug(lcQmlImport) << "addImportPath:" << path;

    if (path.isEmpty())
        return;

    QUrl url = QUrl(path);"""

add_import_new = """void QQmlImportDatabase::addImportPath(const QString& path)
{
    qCDebug(lcQmlImport) << "addImportPath:" << path;

    if (path.isEmpty())
        return;

    if (path.startsWith(QLatin1String("qrc:"), Qt::CaseInsensitive)) {
        QString cPath = path;
        cPath.replace(Backslash, Slash);
        if (!cPath.isEmpty()) {
            if (fileImportPath.contains(cPath))
                fileImportPath.move(fileImportPath.indexOf(cPath), 0);
            else
                fileImportPath.prepend(cPath);
        }
        return;
    }

    QUrl url = QUrl(path);"""

add_plugin_old = """void QQmlImportDatabase::addPluginPath(const QString& path)
{
    qCDebug(lcQmlImport) << "addPluginPath:" << path;

    QUrl url = QUrl(path);"""

add_plugin_new = """void QQmlImportDatabase::addPluginPath(const QString& path)
{
    qCDebug(lcQmlImport) << "addPluginPath:" << path;

    if (path.startsWith(QLatin1String("qrc:"), Qt::CaseInsensitive)) {
        filePluginPath.prepend(path);
        return;
    }

    QUrl url = QUrl(path);"""

url_local_old = """QUrl QQmlImports::urlFromLocalFileOrQrcOrUrl(const QString &file)
{
    QUrl url(QLatin1String(file.at(0) == Colon ? "qrc" : "") + file);

    // We don't support single character schemes as those conflict with windows drive letters.
    if (url.scheme().size() < 2)
        return QUrl::fromLocalFile(file);
    return url;
}"""

url_local_new = """QUrl QQmlImports::urlFromLocalFileOrQrcOrUrl(const QString &file)
{
    if (file.isEmpty())
        return QUrl();
    if (file.startsWith(QLatin1String("qrc:"), Qt::CaseInsensitive))
        return QUrl(file);
    if (file.startsWith(QLatin1Char(':')))
        return QUrl(QLatin1String("qrc") + file);
    QUrl url(QLatin1String(file.at(0) == Colon ? "qrc" : "") + file);

    // We don't support single character schemes as those conflict with windows drive letters.
    if (url.scheme().size() < 2)
        return QUrl::fromLocalFile(file);
    return url;
}"""

setbase_old = """void QQmlImports::setBaseUrl(const QUrl& url, const QString &urlString)
{
    m_baseUrl = url;

    if (urlString.isEmpty())
        m_base = url.toString();
    else
        m_base = urlString;
}"""

setbase_new = """void QQmlImports::setBaseUrl(const QUrl& url, const QString &urlString)
{
    Q_UNUSED(url);
    m_baseUrl = QUrl();

    if (urlString.isEmpty())
        m_base = QStringLiteral("qrc:/GuestMvpShell.qml");
    else
        m_base = urlString;
}"""

changed = 0
for old, new, tag in [
    (add_import_old, add_import_new, "addImportPath"),
    (add_plugin_old, add_plugin_new, "addPluginPath"),
    (url_local_old, url_local_new, "urlFromLocalFileOrQrcOrUrl"),
    (setbase_old, setbase_new, "setBaseUrl"),
]:
    if new in text:
        print(f"[patch_qqmlimport_addpath_guest] {tag}: already applied")
    elif old in text:
        text = text.replace(old, new, 1)
        changed += 1
        print(f"[patch_qqmlimport_addpath_guest] {tag}: ok")
    else:
        raise SystemExit(f"[patch_qqmlimport_addpath_guest] {tag}: anchor missing")

if changed:
    path.write_text(text)
print(f"[patch_qqmlimport_addpath_guest] done ({changed} updated)")
PY
