#!/usr/bin/env python3
"""Guest: capture QUrl strings in QQmlDataBlob ctor before m_finalUrl is corrupted."""
from pathlib import Path
import re

db = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmldatablob.cpp")
te = db.read_text()

# Remove misplaced pre-include helper from failed apply
te = re.sub(
    r"\n#if defined\(BFREE_GUEST_FIXED_STACK\)\nstatic QString bfree_guest_qurl_to_string_safe.*?\n#endif\n",
    "\n",
    te,
    count=1,
    flags=re.DOTALL,
)
te = re.sub(
    r"\n#if defined\(BFREE_GUEST_FIXED_STACK\)\nextern \"C\" void bfree_guest_qv4_heartbeat.*?\n#endif\n\n",
    "\n",
    te,
    count=1,
)

helper = """
#if defined(BFREE_GUEST_FIXED_STACK)
static QString bfree_guest_qurl_to_string_safe(const QUrl &url)
{
    const QString scheme = url.scheme();
    if (scheme.compare(QLatin1String("qrc"), Qt::CaseInsensitive) == 0)
        return QLatin1Char(':') + url.path();
    if (scheme.compare(QLatin1String("file"), Qt::CaseInsensitive) == 0)
        return url.toLocalFile();
    const QString s = url.toString(QUrl::FullyEncoded);
    if (!s.isEmpty())
        return s;
    return QStringLiteral("qrc:/GuestMvpShell.qml");
}
#endif
"""

if "static QString bfree_guest_qurl_to_string_safe" not in te:
    te = te.replace("QT_BEGIN_NAMESPACE\n", "QT_BEGIN_NAMESPACE\n" + helper + "\n", 1)

if 'extern "C" void bfree_guest_qv4_heartbeat' not in te:
    te = te.replace(
        "#include <qtqml_tracepoints_p.h>\n",
        "#include <qtqml_tracepoints_p.h>\n\n"
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        'extern "C" void bfree_guest_qv4_heartbeat(const char *);\n'
        "#endif\n",
        1,
    )

if "bfree_guest_datablob_url_capture" in te and "static QString bfree_guest_qurl_to_string_safe" in te:
    print("[v235] datablob url capture already patched")
    db.write_text(te)
    raise SystemExit(0)

old_ctor = """QQmlDataBlob::QQmlDataBlob(const QUrl &url, Type type, QQmlTypeLoader *manager)
: m_typeLoader(manager), m_type(type), m_url(url), m_finalUrl(url), m_redirectCount(0),
  m_inCallback(false), m_isDone(false)
{
    //Set here because we need to get the engine from the manager
    if (const QQmlEngine *qmlEngine = m_typeLoader->engine())
        m_url = qmlEngine->interceptUrl(m_url, (QQmlAbstractUrlInterceptor::DataType)m_type);
}"""

new_ctor = """QQmlDataBlob::QQmlDataBlob(const QUrl &url, Type type, QQmlTypeLoader *manager)
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

if old_ctor not in te:
    raise SystemExit("[v235] QQmlDataBlob ctor anchor missing")
te = te.replace(old_ctor, new_ctor, 1)

old_final = """QString QQmlDataBlob::finalUrlString() const
{
    if (m_finalUrlString.isEmpty())
        m_finalUrlString = m_finalUrl.toString();

    return m_finalUrlString;
}"""

new_final = """QString QQmlDataBlob::finalUrlString() const
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
}"""

if old_final not in te:
    raise SystemExit("[v235] finalUrlString anchor missing")
te = te.replace(old_final, new_final, 1)

old_url = """QString QQmlDataBlob::urlString() const
{
    if (m_urlString.isEmpty())
        m_urlString = m_url.toString();

    return m_urlString;
}"""

new_url = """QString QQmlDataBlob::urlString() const
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
}"""

if old_url not in te:
    raise SystemExit("[v235] urlString anchor missing")
te = te.replace(old_url, new_url, 1)

db.write_text(te)
print("[v235] datablob url string capture")
