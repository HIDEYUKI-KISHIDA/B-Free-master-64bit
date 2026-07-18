#!/usr/bin/env python3
"""Revert v235 datablob ctor patch; use static compile URL in qqmltypedata on guest."""
from pathlib import Path
import re

db = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmldatablob.cpp")
te = db.read_text()

te = re.sub(
    r"\n#if defined\(BFREE_GUEST_FIXED_STACK\)\nstatic QString bfree_guest_qurl_to_string_safe.*?\n#endif\n",
    "\n",
    te,
    count=1,
    flags=re.DOTALL,
)
te = re.sub(
    r"\n#if defined\(BFREE_GUEST_FIXED_STACK\)\nextern \"C\" void bfree_guest_qv4_heartbeat.*?\n#endif\n",
    "\n",
    te,
    count=1,
)

patched_ctor = """QQmlDataBlob::QQmlDataBlob(const QUrl &url, Type type, QQmlTypeLoader *manager)
: m_typeLoader(manager), m_type(type), m_url(url), m_finalUrl(url), m_redirectCount(0),
  m_inCallback(false), m_isDone(false)
{
    //Set here because we need to get the engine from the manager
    if (const QQmlEngine *qmlEngine = m_typeLoader->engine())
        m_url = qmlEngine->interceptUrl(m_url, (QQmlAbstractUrlInterceptor::DataType)m_type);
#if defined(BFREE_GUEST_FIXED_STACK)
    m_finalUrlString = bfree_guest_qurl_to_string_safe(m_finalUrl);
    m_urlString = bfree_guest_qurl_to_string_safe(m_url);
    bfree_guest_qv4_heartbeat("bfree_guest_datablob_url_capture");
#endif
}"""

plain_ctor = """QQmlDataBlob::QQmlDataBlob(const QUrl &url, Type type, QQmlTypeLoader *manager)
: m_typeLoader(manager), m_type(type), m_url(url), m_finalUrl(url), m_redirectCount(0),
  m_inCallback(false), m_isDone(false)
{
    //Set here because we need to get the engine from the manager
    if (const QQmlEngine *qmlEngine = m_typeLoader->engine())
        m_url = qmlEngine->interceptUrl(m_url, (QQmlAbstractUrlInterceptor::DataType)m_type);
}"""

if patched_ctor in te:
    te = te.replace(patched_ctor, plain_ctor, 1)

te = te.replace(
    """QString QQmlDataBlob::urlString() const
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!m_urlString.isEmpty())
        return m_urlString;
    m_urlString = bfree_guest_qurl_to_string_safe(m_url);
    return m_urlString;
#else
    if (m_urlString.isEmpty())
        m_urlString = m_url.toString();
    return m_urlString;
#endif
}""",
    """QString QQmlDataBlob::urlString() const
{
    if (m_urlString.isEmpty())
        m_urlString = m_url.toString();

    return m_urlString;
}""",
)

te = te.replace(
    """QString QQmlDataBlob::finalUrlString() const
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!m_finalUrlString.isEmpty())
        return m_finalUrlString;
    m_finalUrlString = bfree_guest_qurl_to_string_safe(m_finalUrl);
    return m_finalUrlString;
#else
    if (m_finalUrlString.isEmpty())
        m_finalUrlString = m_finalUrl.toString();
    return m_finalUrlString;
#endif
}""",
    """QString QQmlDataBlob::finalUrlString() const
{
    if (m_finalUrlString.isEmpty())
        m_finalUrlString = m_finalUrl.toString();

    return m_finalUrlString;
}""",
)

db.write_text(te)

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "bfree_guest_static_qml_url" in tt:
    print("[v236] typedata static url already patched")
    raise SystemExit(0)

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

tt = tt.replace("QT_BEGIN_NAMESPACE\n", "QT_BEGIN_NAMESPACE\n" + helper + "\n", 1)

tt = tt.replace(
    "    if (!compiler.generateFromQml(source, finalUrlString(), m_document.data())) {",
    """#if defined(BFREE_GUEST_FIXED_STACK)
    if (!compiler.generateFromQml(source, bfree_guest_static_qml_url(), m_document.data())) {
#else
    if (!compiler.generateFromQml(source, finalUrlString(), m_document.data())) {
#endif""",
    1,
)

old_cont = """    m_importCache->setBaseUrl(finalUrl(), finalUrlString());

    // For remote URLs, we don't delay the loading of the implicit import
    // because the loading probably requires an asynchronous fetch of the
    // qmldir (so we can't load it just in time).
    if (!finalUrl().scheme().isEmpty()) {
        QUrl qmldirUrl = finalUrl().resolved(QUrl(QLatin1String("qmldir")));"""

new_cont = """#if defined(BFREE_GUEST_FIXED_STACK)
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

if old_cont not in tt:
    raise SystemExit("[v236] continueLoadFromIR anchor missing")
tt = tt.replace(old_cont, new_cont, 1)

td.write_text(tt)
print("[v236] revert datablob + typedata static qml url")
