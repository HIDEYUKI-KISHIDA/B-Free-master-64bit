#!/usr/bin/env python3
"""Guest: setBaseUrl without copying QUrl; typedata uses string-only base."""
from pathlib import Path

imp = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmlimport.cpp")
ie = imp.read_text()

old = """void QQmlImports::setBaseUrl(const QUrl& url, const QString &urlString)
{
    m_baseUrl = url;

    if (urlString.isEmpty())
        m_base = url.toString();
    else
        m_base = urlString;
}"""

new = """void QQmlImports::setBaseUrl(const QUrl& url, const QString &urlString)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    (void)url;
    m_base = urlString.isEmpty() ? QStringLiteral("qrc:/GuestMvpShell.qml") : urlString;
    m_baseUrl = QUrl();
#else
    m_baseUrl = url;

    if (urlString.isEmpty())
        m_base = url.toString();
    else
        m_base = urlString;
#endif
}"""

if "BFREE_GUEST_FIXED_STACK" not in ie or old in ie:
    if old not in ie:
        raise SystemExit("[v242] setBaseUrl anchor missing")
    ie = ie.replace(old, new, 1)
    imp.write_text(ie)
    print("[v242] qqmlimports setBaseUrl guest")
else:
    print("[v242] setBaseUrl already patched")

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

tt = tt.replace(
    "    m_importCache->setBaseUrl(bfree_guest_static_qml_qurl(), bfree_guest_static_qml_url());",
    "    m_importCache->setBaseUrl(QUrl(), bfree_guest_static_qml_url());",
)

old_loop = """    for (auto const& object: m_document->objects) {
        for (auto it = object->inlineComponentsBegin(); it != object->inlineComponentsEnd(); ++it) {
            QString const nameString = m_document->stringAt(it->nameIndex);
#if defined(BFREE_GUEST_FIXED_STACK)
            auto importUrl = bfree_guest_static_qml_qurl();
#else
            auto importUrl = finalUrl();
#endif
            importUrl.setFragment(nameString);
            auto import = new QQmlImportInstance(); // Note: The cache takes ownership of the QQmlImportInstance
            m_importCache->addInlineComponentImport(import, nameString, importUrl);
        }
    }"""

new_loop = """#if !defined(BFREE_GUEST_FIXED_STACK)
    for (auto const& object: m_document->objects) {
        for (auto it = object->inlineComponentsBegin(); it != object->inlineComponentsEnd(); ++it) {
            QString const nameString = m_document->stringAt(it->nameIndex);
            auto importUrl = finalUrl();
            importUrl.setFragment(nameString);
            auto import = new QQmlImportInstance(); // Note: The cache takes ownership of the QQmlImportInstance
            m_importCache->addInlineComponentImport(import, nameString, importUrl);
        }
    }
#endif"""

if old_loop in tt:
    tt = tt.replace(old_loop, new_loop, 1)
    td.write_text(tt)
    print("[v242] typedata skip inline imports on guest")
elif "#if !defined(BFREE_GUEST_FIXED_STACK)" in tt and "inlineComponentsBegin" in tt:
    print("[v242] typedata inline skip already patched")
    td.write_text(tt)
else:
    raise SystemExit("[v242] inline loop anchor missing")
