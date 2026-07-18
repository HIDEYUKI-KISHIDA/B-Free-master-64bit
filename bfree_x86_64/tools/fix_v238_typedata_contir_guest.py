#!/usr/bin/env python3
"""Guest: static qml url in continueLoadFromIR (v236 anchor fix)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

helper = """
#if defined(BFREE_GUEST_FIXED_STACK)
static inline QString bfree_guest_static_qml_url()
{
    return QStringLiteral("qrc:/GuestMvpShell.qml");
}

static inline QUrl bfree_guest_static_qml_qurl()
{
    static const QUrl url(QStringLiteral("qrc:/GuestMvpShell.qml"));
    return url;
}
#endif

"""

if "bfree_guest_static_qml_url" not in tt:
    tt = tt.replace("QT_BEGIN_NAMESPACE\n", "QT_BEGIN_NAMESPACE\n" + helper + "\n", 1)

if "bfree_guest_static_qml_url()" not in tt:
    tt = tt.replace(
        "    if (!compiler.generateFromQml(source, finalUrlString(), m_document.data())) {",
        """#if defined(BFREE_GUEST_FIXED_STACK)
    if (!compiler.generateFromQml(source, bfree_guest_static_qml_url(), m_document.data())) {
#else
    if (!compiler.generateFromQml(source, finalUrlString(), m_document.data())) {
#endif""",
        1,
    )

old_cont = """    m_typeReferences.collectFromObjects(m_document->objects.constBegin(), m_document->objects.constEnd());
    m_importCache->setBaseUrl(finalUrl(), finalUrlString());

    // For remote URLs, we don't delay the loading of the implicit import
    // because the loading probably requires an asynchronous fetch of the
    // qmldir (so we can't load it just in time).
    if (!finalUrl().scheme().isEmpty()) {
        QUrl qmldirUrl = finalUrl().resolved(QUrl(QLatin1String("qmldir")));"""

new_cont = """    m_typeReferences.collectFromObjects(m_document->objects.constBegin(), m_document->objects.constEnd());
#if defined(BFREE_GUEST_FIXED_STACK)
    m_importCache->setBaseUrl(bfree_guest_static_qml_qurl(), bfree_guest_static_qml_url());
#else
    m_importCache->setBaseUrl(finalUrl(), finalUrlString());
#endif

    // For remote URLs, we don't delay the loading of the implicit import
    // because the loading probably requires an asynchronous fetch of the
    // qmldir (so we can't load it just in time).
#if defined(BFREE_GUEST_FIXED_STACK)
    if (true) {
        QUrl qmldirUrl = bfree_guest_static_qml_qurl().resolved(QUrl(QLatin1String("qmldir")));
#else
    if (!finalUrl().scheme().isEmpty()) {
        QUrl qmldirUrl = finalUrl().resolved(QUrl(QLatin1String("qmldir")));
#endif"""

if old_cont in tt:
    tt = tt.replace(old_cont, new_cont, 1)
elif "bfree_guest_static_qml_qurl(), bfree_guest_static_qml_url()" in tt:
    print("[v238] continueLoadFromIR static url already patched")
else:
    raise SystemExit("[v238] continueLoadFromIR anchor missing")

old_inline = """            auto importUrl = finalUrl();
            importUrl.setFragment(nameString);"""

new_inline = """#if defined(BFREE_GUEST_FIXED_STACK)
            auto importUrl = bfree_guest_static_qml_qurl();
#else
            auto importUrl = finalUrl();
#endif
            importUrl.setFragment(nameString);"""

if old_inline in tt:
    tt = tt.replace(old_inline, new_inline, 1)

td.write_text(tt)
print("[v238] continueLoadFromIR static qml url")
