from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

proto_old = """    bfree_guest_qv4_heartbeat("proto_init_enter");
    if (jsObjects[Object_Ctor].isManaged())
        static_cast<ObjectPrototype *>(objectPrototype())->init(this, objectCtor());
    if (jsObjects[String_Ctor].isManaged())
        static_cast<StringPrototype *>(stringPrototype())->init(this, stringCtor());
    if (jsObjects[Symbol_Ctor].isManaged())
        static_cast<SymbolPrototype *>(symbolPrototype())->init(this, symbolCtor());
    if (jsObjects[Number_Ctor].isManaged())
        static_cast<NumberPrototype *>(numberPrototype())->init(this, numberCtor());
    if (jsObjects[Boolean_Ctor].isManaged())
        static_cast<BooleanPrototype *>(booleanPrototype())->init(this, booleanCtor());
    if (jsObjects[Array_Ctor].isManaged())
        static_cast<ArrayPrototype *>(arrayPrototype())->init(this, arrayCtor());
    static_cast<PropertyListPrototype *>(propertyListPrototype())->init();
    if (jsObjects[Date_Ctor].isManaged())
        static_cast<DatePrototype *>(datePrototype())->init(this, dateCtor());
    if (jsObjects[IteratorProto].isManaged())
        static_cast<IteratorPrototype *>(iteratorPrototype())->init(this);
    if (jsObjects[VariantProto].isManaged())
        static_cast<VariantPrototype *>(variantPrototype())->init();
    if (jsObjects[SequenceProto].isManaged())
        sequencePrototype()->cast<SequencePrototype>()->init();
    bfree_guest_qv4_heartbeat("proto_init_partial");"""

proto_new = """    bfree_guest_qv4_heartbeat("proto_init_enter");
    bfree_guest_qv4_heartbeat("proto_init_skip");"""

tail_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    FunctionObject *numberObject = jsObjects[Number_Ctor].isManaged() ? numberCtor() : nullptr;
#else
    FunctionObject *numberObject = numberCtor();
#endif
    ScopedObject o(scope);
    globalObject->defineDefaultProperty(QStringLiteral("Atomics"), (o = memoryManager->allocate<Atomics>()));
    globalObject->defineDefaultProperty(QStringLiteral("Math"), (o = memoryManager->allocate<MathObject>()));
    globalObject->defineDefaultProperty(QStringLiteral("JSON"), (o = memoryManager->allocate<JsonObject>()));
    globalObject->defineDefaultProperty(QStringLiteral("Reflect"), (o = memoryManager->allocate<Reflect>()));
    globalObject->defineDefaultProperty(QStringLiteral("Proxy"), (o = memoryManager->allocate<Proxy>(this)));

    globalObject->defineReadonlyProperty(QStringLiteral("undefined"), Value::undefinedValue());
    globalObject->defineReadonlyProperty(QStringLiteral("NaN"), Value::fromDouble(std::numeric_limits<double>::quiet_NaN()));
    globalObject->defineReadonlyProperty(QStringLiteral("Infinity"), Value::fromDouble(Q_INFINITY));


    jsObjects[Eval_Function] = memoryManager->allocate<EvalFunction>(this);
    globalObject->defineDefaultProperty(QStringLiteral("eval"), *evalFunction());

    // ES6: 20.1.2.12 &  20.1.2.13:
    // parseInt and parseFloat must be the same FunctionObject on the global &
    // Number object.
    {
        QString piString(QStringLiteral("parseInt"));
        QString pfString(QStringLiteral("parseFloat"));
        Scope scope(this);
        ScopedString pi(scope, newIdentifier(piString));
        ScopedString pf(scope, newIdentifier(pfString));
        ScopedFunctionObject parseIntFn(scope, FunctionObject::createBuiltinFunction(this, pi, GlobalFunctions::method_parseInt, 2));
        ScopedFunctionObject parseFloatFn(scope, FunctionObject::createBuiltinFunction(this, pf, GlobalFunctions::method_parseFloat, 1));
        globalObject->defineDefaultProperty(piString, parseIntFn);
        globalObject->defineDefaultProperty(pfString, parseFloatFn);
        numberObject->defineDefaultProperty(piString, parseIntFn);
        numberObject->defineDefaultProperty(pfString, parseFloatFn);
    }

    globalObject->defineDefaultProperty(QStringLiteral("isNaN"), GlobalFunctions::method_isNaN, 1);
    globalObject->defineDefaultProperty(QStringLiteral("isFinite"), GlobalFunctions::method_isFinite, 1);
    globalObject->defineDefaultProperty(QStringLiteral("decodeURI"), GlobalFunctions::method_decodeURI, 1);
    globalObject->defineDefaultProperty(QStringLiteral("decodeURIComponent"), GlobalFunctions::method_decodeURIComponent, 1);
    globalObject->defineDefaultProperty(QStringLiteral("encodeURI"), GlobalFunctions::method_encodeURI, 1);
    globalObject->defineDefaultProperty(QStringLiteral("encodeURIComponent"), GlobalFunctions::method_encodeURIComponent, 1);
    globalObject->defineDefaultProperty(QStringLiteral("escape"), GlobalFunctions::method_escape, 1);
    globalObject->defineDefaultProperty(QStringLiteral("unescape"), GlobalFunctions::method_unescape, 1);

    ScopedFunctionObject t(
            scope,
            memoryManager->allocate<DynamicFunctionObject>(this, nullptr, ::throwTypeError));
    t->defineReadonlyProperty(id_length(), Value::fromInt32(0));
    t->setInternalClass(t->internalClass()->cryopreserved());
    jsObjects[ThrowerObject] = t;

    ScopedProperty pd(scope);
    pd->value = thrower();
    pd->set = thrower();
    functionPrototype()->insertMember(id_caller(), pd, Attr_Accessor|Attr_ReadOnly_ButConfigurable);
    functionPrototype()->insertMember(id_arguments(), pd, Attr_Accessor|Attr_ReadOnly_ButConfigurable);

    QV4::QObjectWrapper::initializeBindings(this);"""

tail_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    FunctionObject *numberObject = jsObjects[Number_Ctor].isManaged() ? numberCtor() : nullptr;
    bfree_guest_qv4_heartbeat("globals_tail_skip");
    QV4::QObjectWrapper::initializeBindings(this);
#else
    FunctionObject *numberObject = numberCtor();
    ScopedObject o(scope);
    globalObject->defineDefaultProperty(QStringLiteral("Atomics"), (o = memoryManager->allocate<Atomics>()));
    globalObject->defineDefaultProperty(QStringLiteral("Math"), (o = memoryManager->allocate<MathObject>()));
    globalObject->defineDefaultProperty(QStringLiteral("JSON"), (o = memoryManager->allocate<JsonObject>()));
    globalObject->defineDefaultProperty(QStringLiteral("Reflect"), (o = memoryManager->allocate<Reflect>()));
    globalObject->defineDefaultProperty(QStringLiteral("Proxy"), (o = memoryManager->allocate<Proxy>(this)));

    globalObject->defineReadonlyProperty(QStringLiteral("undefined"), Value::undefinedValue());
    globalObject->defineReadonlyProperty(QStringLiteral("NaN"), Value::fromDouble(std::numeric_limits<double>::quiet_NaN()));
    globalObject->defineReadonlyProperty(QStringLiteral("Infinity"), Value::fromDouble(Q_INFINITY));


    jsObjects[Eval_Function] = memoryManager->allocate<EvalFunction>(this);
    globalObject->defineDefaultProperty(QStringLiteral("eval"), *evalFunction());

    // ES6: 20.1.2.12 &  20.1.2.13:
    // parseInt and parseFloat must be the same FunctionObject on the global &
    // Number object.
    {
        QString piString(QStringLiteral("parseInt"));
        QString pfString(QStringLiteral("parseFloat"));
        Scope scope(this);
        ScopedString pi(scope, newIdentifier(piString));
        ScopedString pf(scope, newIdentifier(pfString));
        ScopedFunctionObject parseIntFn(scope, FunctionObject::createBuiltinFunction(this, pi, GlobalFunctions::method_parseInt, 2));
        ScopedFunctionObject parseFloatFn(scope, FunctionObject::createBuiltinFunction(this, pf, GlobalFunctions::method_parseFloat, 1));
        globalObject->defineDefaultProperty(piString, parseIntFn);
        globalObject->defineDefaultProperty(pfString, parseFloatFn);
        numberObject->defineDefaultProperty(piString, parseIntFn);
        numberObject->defineDefaultProperty(pfString, parseFloatFn);
    }

    globalObject->defineDefaultProperty(QStringLiteral("isNaN"), GlobalFunctions::method_isNaN, 1);
    globalObject->defineDefaultProperty(QStringLiteral("isFinite"), GlobalFunctions::method_isFinite, 1);
    globalObject->defineDefaultProperty(QStringLiteral("decodeURI"), GlobalFunctions::method_decodeURI, 1);
    globalObject->defineDefaultProperty(QStringLiteral("decodeURIComponent"), GlobalFunctions::method_decodeURIComponent, 1);
    globalObject->defineDefaultProperty(QStringLiteral("encodeURI"), GlobalFunctions::method_encodeURI, 1);
    globalObject->defineDefaultProperty(QStringLiteral("encodeURIComponent"), GlobalFunctions::method_encodeURIComponent, 1);
    globalObject->defineDefaultProperty(QStringLiteral("escape"), GlobalFunctions::method_escape, 1);
    globalObject->defineDefaultProperty(QStringLiteral("unescape"), GlobalFunctions::method_unescape, 1);

    ScopedFunctionObject t(
            scope,
            memoryManager->allocate<DynamicFunctionObject>(this, nullptr, ::throwTypeError));
    t->defineReadonlyProperty(id_length(), Value::fromInt32(0));
    t->setInternalClass(t->internalClass()->cryopreserved());
    jsObjects[ThrowerObject] = t;

    ScopedProperty pd(scope);
    pd->value = thrower();
    pd->set = thrower();
    functionPrototype()->insertMember(id_caller(), pd, Attr_Accessor|Attr_ReadOnly_ButConfigurable);
    functionPrototype()->insertMember(id_arguments(), pd, Attr_Accessor|Attr_ReadOnly_ButConfigurable);

    QV4::QObjectWrapper::initializeBindings(this);
#endif"""

changed = False
if "proto_init_skip" in te:
    print("[v225] proto init skip already patched")
elif proto_old not in te:
    raise SystemExit("[v225] proto init anchor missing")
else:
    te = te.replace(proto_old, proto_new, 1)
    changed = True
    print("[v225] proto init skip")

if "globals_tail_skip" in te:
    print("[v225] globals tail skip already patched")
elif tail_old not in te:
    raise SystemExit("[v225] globals tail anchor missing")
else:
    te = te.replace(tail_old, tail_new, 1)
    changed = True
    print("[v225] globals tail skip")

if changed:
    eng.write_text(te)
print("[v225] done")
