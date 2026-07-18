#!/usr/bin/env python3
"""Guest: skip done() post-compile finalUrl/qmlType checks (QUrl::fileName crash)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_done_post_compile" in tt:
    print("[v265] done post-compile skip already patched")
    raise SystemExit(0)

old = """    {
        QQmlEnginePrivate *const enginePrivate = QQmlEnginePrivate::get(typeLoader()->engine());
        m_compiledData->inlineComponentData = m_inlineComponentData;
        {
            // Sanity check property bindings
            QQmlPropertyValidator validator(enginePrivate, m_importCache.data(), m_compiledData);
            QVector<QQmlError> errors = validator.validate();
            if (!errors.isEmpty()) {
                setError(errors);
                return;
            }
        }

        m_compiledData->finalizeCompositeType(qmlType());
    }

    {
        QQmlType type = QQmlMetaType::qmlType(finalUrl(), true);"""

new = """#if !defined(BFREE_GUEST_FIXED_STACK)
    {
        QQmlEnginePrivate *const enginePrivate = QQmlEnginePrivate::get(typeLoader()->engine());
        m_compiledData->inlineComponentData = m_inlineComponentData;
        {
            // Sanity check property bindings
            QQmlPropertyValidator validator(enginePrivate, m_importCache.data(), m_compiledData);
            QVector<QQmlError> errors = validator.validate();
            if (!errors.isEmpty()) {
                setError(errors);
                return;
            }
        }

        m_compiledData->finalizeCompositeType(qmlType());
    }
#else
    bfree_guest_qv4_heartbeat("typedata_done_post_compile");
#endif

#if !defined(BFREE_GUEST_FIXED_STACK)
    {
        QQmlType type = QQmlMetaType::qmlType(finalUrl(), true);"""

if old not in tt:
    raise SystemExit("[v265] done post-compile anchor missing")
tt = tt.replace(old, new, 1)

old2 = """        }
    }

    {
        // Collect imported scripts
        m_compiledData->dependentScripts.reserve(m_scripts.size());"""

new2 = """        }
    }
#else
    bfree_guest_qv4_heartbeat("typedata_done_skip_singleton");
#endif

#if !defined(BFREE_GUEST_FIXED_STACK)
    {
        // Collect imported scripts
        m_compiledData->dependentScripts.reserve(m_scripts.size());"""

if old2 not in tt:
    raise SystemExit("[v265] done scripts anchor missing")
tt = tt.replace(old2, new2, 1)

old3 = """            m_compiledData->dependentScripts << scriptData;
        }
    }
}

void QQmlTypeData::completed()"""

new3 = """            m_compiledData->dependentScripts << scriptData;
        }
    }
#else
    bfree_guest_qv4_heartbeat("typedata_done_guest_ok");
#endif
}

void QQmlTypeData::completed()"""

if old3 not in tt:
    raise SystemExit("[v265] done end anchor missing")
tt = tt.replace(old3, new3, 1)

td.write_text(tt)
print("[v265] done() post-compile guest skip")
