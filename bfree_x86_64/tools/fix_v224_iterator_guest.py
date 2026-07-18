from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

nic_old = """    Heap::InternalClass *ic = base->changeVTable(vtable);
    return ic ? ic->changePrototype(prototype ? prototype->d() : nullptr) : nullptr;"""

nic_new = """    Heap::InternalClass *ic = base->changeVTable(vtable);
    if (!ic)
        return nullptr;
    Heap::Object *protoHeap = nullptr;
    if (prototype) {
        const Value *pv = reinterpret_cast<const Value *>(prototype);
        if (pv->isManaged())
            protoHeap = static_cast<Heap::Object *>(pv->m());
    }
    return ic->changePrototype(protoHeap);"""

tail_old = """    ic = newInternalClass(ForInIteratorPrototype::staticVTable(), iteratorPrototype());
    jsObjects[ForInIteratorProto] = memoryManager->allocObject<ForInIteratorPrototype>(ic);
    ic = newInternalClass(SetIteratorPrototype::staticVTable(), iteratorPrototype());
    jsObjects[MapIteratorProto] = memoryManager->allocObject<MapIteratorPrototype>(ic);
    ic = newInternalClass(SetIteratorPrototype::staticVTable(), iteratorPrototype());
    jsObjects[SetIteratorProto] = memoryManager->allocObject<SetIteratorPrototype>(ic);
    ic = newInternalClass(ArrayIteratorPrototype::staticVTable(), iteratorPrototype());
    jsObjects[ArrayIteratorProto] = memoryManager->allocObject<ArrayIteratorPrototype>(ic);
    ic = newInternalClass(StringIteratorPrototype::staticVTable(), iteratorPrototype());
    jsObjects[StringIteratorProto] = memoryManager->allocObject<StringIteratorPrototype>(ic);

    //
    // url
    //

    jsObjects[Url_Ctor] = memoryManager->allocate<UrlCtor>(this);
    jsObjects[UrlProto] = memoryManager->allocate<UrlPrototype>();
    jsObjects[UrlSearchParams_Ctor] = memoryManager->allocate<UrlSearchParamsCtor>(this);
    jsObjects[UrlSearchParamsProto] = memoryManager->allocate<UrlSearchParamsPrototype>();

    str = newString(QStringLiteral("get [Symbol.species]"));
    jsObjects[GetSymbolSpecies] = FunctionObject::createBuiltinFunction(this, str, ArrayPrototype::method_get_species, 0);

    static_cast<ObjectPrototype *>(objectPrototype())->init(this, objectCtor());
    static_cast<StringPrototype *>(stringPrototype())->init(this, stringCtor());
    static_cast<SymbolPrototype *>(symbolPrototype())->init(this, symbolCtor());
    static_cast<NumberPrototype *>(numberPrototype())->init(this, numberCtor());
    static_cast<BooleanPrototype *>(booleanPrototype())->init(this, booleanCtor());
    static_cast<ArrayPrototype *>(arrayPrototype())->init(this, arrayCtor());
    static_cast<PropertyListPrototype *>(propertyListPrototype())->init();
    static_cast<DatePrototype *>(datePrototype())->init(this, dateCtor());
    static_cast<FunctionPrototype *>(functionPrototype())->init(this, functionCtor());
    static_cast<GeneratorPrototype *>(generatorPrototype())->init(this, generatorFunctionCtor());
    static_cast<RegExpPrototype *>(regExpPrototype())->init(this, regExpCtor());
    static_cast<ErrorPrototype *>(errorPrototype())->init(this, errorCtor());
    static_cast<EvalErrorPrototype *>(evalErrorPrototype())->init(this, evalErrorCtor());
    static_cast<RangeErrorPrototype *>(rangeErrorPrototype())->init(this, rangeErrorCtor());
    static_cast<ReferenceErrorPrototype *>(referenceErrorPrototype())->init(this, referenceErrorCtor());
    static_cast<SyntaxErrorPrototype *>(syntaxErrorPrototype())->init(this, syntaxErrorCtor());
    static_cast<TypeErrorPrototype *>(typeErrorPrototype())->init(this, typeErrorCtor());
    static_cast<URIErrorPrototype *>(uRIErrorPrototype())->init(this, uRIErrorCtor());
    static_cast<UrlPrototype *>(urlPrototype())->init(this, urlCtor());
    static_cast<UrlSearchParamsPrototype *>(urlSearchParamsPrototype())->init(this, urlSearchParamsCtor());

    static_cast<IteratorPrototype *>(iteratorPrototype())->init(this);
    static_cast<ForInIteratorPrototype *>(forInIteratorPrototype())->init(this);
    static_cast<MapIteratorPrototype *>(mapIteratorPrototype())->init(this);
    static_cast<SetIteratorPrototype *>(setIteratorPrototype())->init(this);
    static_cast<ArrayIteratorPrototype *>(arrayIteratorPrototype())->init(this);
    static_cast<StringIteratorPrototype *>(stringIteratorPrototype())->init(this);

    static_cast<VariantPrototype *>(variantPrototype())->init();

    sequencePrototype()->cast<SequencePrototype>()->init();

    jsObjects[WeakMap_Ctor] = memoryManager->allocate<WeakMapCtor>(this);
    jsObjects[WeakMapProto] = memoryManager->allocate<WeakMapPrototype>();
    static_cast<WeakMapPrototype *>(weakMapPrototype())->init(this, weakMapCtor());

    jsObjects[Map_Ctor] = memoryManager->allocate<MapCtor>(this);
    jsObjects[MapProto] = memoryManager->allocate<MapPrototype>();
    static_cast<MapPrototype *>(mapPrototype())->init(this, mapCtor());

    jsObjects[WeakSet_Ctor] = memoryManager->allocate<WeakSetCtor>(this);
    jsObjects[WeakSetProto] = memoryManager->allocate<WeakSetPrototype>();
    static_cast<WeakSetPrototype *>(weakSetPrototype())->init(this, weakSetCtor());

    jsObjects[Set_Ctor] = memoryManager->allocate<SetCtor>(this);
    jsObjects[SetProto] = memoryManager->allocate<SetPrototype>();
    static_cast<SetPrototype *>(setPrototype())->init(this, setCtor());

    //
    // promises
    //

    jsObjects[Promise_Ctor] = memoryManager->allocate<PromiseCtor>(this);
    jsObjects[PromiseProto] = memoryManager->allocate<PromisePrototype>();
    static_cast<PromisePrototype *>(promisePrototype())->init(this, promiseCtor());

    // typed arrays

    jsObjects[SharedArrayBuffer_Ctor] = memoryManager->allocate<SharedArrayBufferCtor>(this);
    jsObjects[SharedArrayBufferProto] = memoryManager->allocate<SharedArrayBufferPrototype>();
    static_cast<SharedArrayBufferPrototype *>(sharedArrayBufferPrototype())->init(this, sharedArrayBufferCtor());

    jsObjects[ArrayBuffer_Ctor] = memoryManager->allocate<ArrayBufferCtor>(this);
    jsObjects[ArrayBufferProto] = memoryManager->allocate<ArrayBufferPrototype>();
    static_cast<ArrayBufferPrototype *>(arrayBufferPrototype())->init(this, arrayBufferCtor());

    jsObjects[DataView_Ctor] = memoryManager->allocate<DataViewCtor>(this);
    jsObjects[DataViewProto] = memoryManager->allocate<DataViewPrototype>();
    static_cast<DataViewPrototype *>(dataViewPrototype())->init(this, dataViewCtor());
    jsObjects[ValueTypeProto] = (Heap::Base *) nullptr;
    jsObjects[SignalHandlerProto] = (Heap::Base *) nullptr;
    jsObjects[TypeWrapperProto] = (Heap::Base *) nullptr;

    jsObjects[IntrinsicTypedArray_Ctor] = memoryManager->allocate<IntrinsicTypedArrayCtor>(this);
    jsObjects[IntrinsicTypedArrayProto] = memoryManager->allocate<IntrinsicTypedArrayPrototype>();
    static_cast<IntrinsicTypedArrayPrototype *>(intrinsicTypedArrayPrototype())
            ->init(this, static_cast<IntrinsicTypedArrayCtor *>(intrinsicTypedArrayCtor()));

    for (int i = 0; i < NTypedArrayTypes; ++i) {
        static_cast<Value &>(typedArrayCtors[i]) = memoryManager->allocate<TypedArrayCtor>(this, Heap::TypedArray::Type(i));
        static_cast<Value &>(typedArrayPrototype[i]) = memoryManager->allocate<TypedArrayPrototype>(Heap::TypedArray::Type(i));
        typedArrayPrototype[i].as<TypedArrayPrototype>()->init(this, static_cast<TypedArrayCtor *>(typedArrayCtors[i].as<Object>()));
    }

    //
    // set up the global object
    //
    rootContext()->d()->activation.set(scope.engine, globalObject->d());
    Q_ASSERT(globalObject->d()->vtable());

    globalObject->defineDefaultProperty(QStringLiteral("Object"), *objectCtor());
    globalObject->defineDefaultProperty(QStringLiteral("String"), *stringCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Symbol"), *symbolCtor());
    FunctionObject *numberObject = numberCtor();
    globalObject->defineDefaultProperty(QStringLiteral("Number"), *numberObject);
    globalObject->defineDefaultProperty(QStringLiteral("Boolean"), *booleanCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Array"), *arrayCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Function"), *functionCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Date"), *dateCtor());
    globalObject->defineDefaultProperty(QStringLiteral("RegExp"), *regExpCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Error"), *errorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("EvalError"), *evalErrorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("RangeError"), *rangeErrorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("ReferenceError"), *referenceErrorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("SyntaxError"), *syntaxErrorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("TypeError"), *typeErrorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("URIError"), *uRIErrorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Promise"), *promiseCtor());
    globalObject->defineDefaultProperty(QStringLiteral("URL"), *urlCtor());
    globalObject->defineDefaultProperty(QStringLiteral("URLSearchParams"), *urlSearchParamsCtor());

    globalObject->defineDefaultProperty(QStringLiteral("SharedArrayBuffer"), *sharedArrayBufferCtor());
    globalObject->defineDefaultProperty(QStringLiteral("ArrayBuffer"), *arrayBufferCtor());
    globalObject->defineDefaultProperty(QStringLiteral("DataView"), *dataViewCtor());
    globalObject->defineDefaultProperty(QStringLiteral("WeakSet"), *weakSetCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Set"), *setCtor());
    globalObject->defineDefaultProperty(QStringLiteral("WeakMap"), *weakMapCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Map"), *mapCtor());

    for (int i = 0; i < NTypedArrayTypes; ++i)
        globalObject->defineDefaultProperty((str = typedArrayCtors[i].as<FunctionObject>()->name()), typedArrayCtors[i]);"""

tail_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("iterator_ic_enter");
    {
        Heap::Object *itHeap = jsObjects[IteratorProto].isManaged()
                ? static_cast<Heap::Object *>(jsObjects[IteratorProto].m()) : nullptr;
        if (itHeap && classes[Class_Empty]) {
            if (Heap::InternalClass *fic = classes[Class_Empty]->changeVTable(ForInIteratorPrototype::staticVTable())) {
                fic = fic->changePrototype(itHeap);
                if (fic)
                    jsObjects[ForInIteratorProto] = memoryManager->allocObject<ForInIteratorPrototype>(fic);
            }
        }
        bfree_guest_qv4_heartbeat(jsObjects[ForInIteratorProto].isManaged() ? "forin_iter_ok" : "forin_iter_null");
        bfree_guest_qv4_heartbeat("iterator_sub_skip");
    }
    bfree_guest_qv4_heartbeat("proto_init_enter");
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
    bfree_guest_qv4_heartbeat("proto_init_partial");
    bfree_guest_qv4_heartbeat("collections_skip");
    bfree_guest_qv4_heartbeat("global_setup_enter");
    if (globalObject && rootContext() && rootContext()->d()) {
        Heap::Object *gHeap = static_cast<Heap::Object *>(globalObject->d());
        if (gHeap)
            *reinterpret_cast<Heap::Object **>(&rootContext()->d()->activation) = gHeap;
    }
    if (globalObject) {
        if (jsObjects[Object_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("Object"), *objectCtor());
        if (jsObjects[String_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("String"), *stringCtor());
        if (jsObjects[Symbol_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("Symbol"), *symbolCtor());
        if (jsObjects[Number_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("Number"), *numberCtor());
        if (jsObjects[Boolean_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("Boolean"), *booleanCtor());
        if (jsObjects[Array_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("Array"), *arrayCtor());
        if (jsObjects[Date_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("Date"), *dateCtor());
    }
    bfree_guest_qv4_heartbeat("global_setup_partial");
#else
    ic = newInternalClass(ForInIteratorPrototype::staticVTable(), iteratorPrototype());
    jsObjects[ForInIteratorProto] = memoryManager->allocObject<ForInIteratorPrototype>(ic);
    ic = newInternalClass(SetIteratorPrototype::staticVTable(), iteratorPrototype());
    jsObjects[MapIteratorProto] = memoryManager->allocObject<MapIteratorPrototype>(ic);
    ic = newInternalClass(SetIteratorPrototype::staticVTable(), iteratorPrototype());
    jsObjects[SetIteratorProto] = memoryManager->allocObject<SetIteratorPrototype>(ic);
    ic = newInternalClass(ArrayIteratorPrototype::staticVTable(), iteratorPrototype());
    jsObjects[ArrayIteratorProto] = memoryManager->allocObject<ArrayIteratorPrototype>(ic);
    ic = newInternalClass(StringIteratorPrototype::staticVTable(), iteratorPrototype());
    jsObjects[StringIteratorProto] = memoryManager->allocObject<StringIteratorPrototype>(ic);

    //
    // url
    //

    jsObjects[Url_Ctor] = memoryManager->allocate<UrlCtor>(this);
    jsObjects[UrlProto] = memoryManager->allocate<UrlPrototype>();
    jsObjects[UrlSearchParams_Ctor] = memoryManager->allocate<UrlSearchParamsCtor>(this);
    jsObjects[UrlSearchParamsProto] = memoryManager->allocate<UrlSearchParamsPrototype>();

    str = newString(QStringLiteral("get [Symbol.species]"));
    jsObjects[GetSymbolSpecies] = FunctionObject::createBuiltinFunction(this, str, ArrayPrototype::method_get_species, 0);

    static_cast<ObjectPrototype *>(objectPrototype())->init(this, objectCtor());
    static_cast<StringPrototype *>(stringPrototype())->init(this, stringCtor());
    static_cast<SymbolPrototype *>(symbolPrototype())->init(this, symbolCtor());
    static_cast<NumberPrototype *>(numberPrototype())->init(this, numberCtor());
    static_cast<BooleanPrototype *>(booleanPrototype())->init(this, booleanCtor());
    static_cast<ArrayPrototype *>(arrayPrototype())->init(this, arrayCtor());
    static_cast<PropertyListPrototype *>(propertyListPrototype())->init();
    static_cast<DatePrototype *>(datePrototype())->init(this, dateCtor());
    static_cast<FunctionPrototype *>(functionPrototype())->init(this, functionCtor());
    static_cast<GeneratorPrototype *>(generatorPrototype())->init(this, generatorFunctionCtor());
    static_cast<RegExpPrototype *>(regExpPrototype())->init(this, regExpCtor());
    static_cast<ErrorPrototype *>(errorPrototype())->init(this, errorCtor());
    static_cast<EvalErrorPrototype *>(evalErrorPrototype())->init(this, evalErrorCtor());
    static_cast<RangeErrorPrototype *>(rangeErrorPrototype())->init(this, rangeErrorCtor());
    static_cast<ReferenceErrorPrototype *>(referenceErrorPrototype())->init(this, referenceErrorCtor());
    static_cast<SyntaxErrorPrototype *>(syntaxErrorPrototype())->init(this, syntaxErrorCtor());
    static_cast<TypeErrorPrototype *>(typeErrorPrototype())->init(this, typeErrorCtor());
    static_cast<URIErrorPrototype *>(uRIErrorPrototype())->init(this, uRIErrorCtor());
    static_cast<UrlPrototype *>(urlPrototype())->init(this, urlCtor());
    static_cast<UrlSearchParamsPrototype *>(urlSearchParamsPrototype())->init(this, urlSearchParamsCtor());

    static_cast<IteratorPrototype *>(iteratorPrototype())->init(this);
    static_cast<ForInIteratorPrototype *>(forInIteratorPrototype())->init(this);
    static_cast<MapIteratorPrototype *>(mapIteratorPrototype())->init(this);
    static_cast<SetIteratorPrototype *>(setIteratorPrototype())->init(this);
    static_cast<ArrayIteratorPrototype *>(arrayIteratorPrototype())->init(this);
    static_cast<StringIteratorPrototype *>(stringIteratorPrototype())->init(this);

    static_cast<VariantPrototype *>(variantPrototype())->init();

    sequencePrototype()->cast<SequencePrototype>()->init();

    jsObjects[WeakMap_Ctor] = memoryManager->allocate<WeakMapCtor>(this);
    jsObjects[WeakMapProto] = memoryManager->allocate<WeakMapPrototype>();
    static_cast<WeakMapPrototype *>(weakMapPrototype())->init(this, weakMapCtor());

    jsObjects[Map_Ctor] = memoryManager->allocate<MapCtor>(this);
    jsObjects[MapProto] = memoryManager->allocate<MapPrototype>();
    static_cast<MapPrototype *>(mapPrototype())->init(this, mapCtor());

    jsObjects[WeakSet_Ctor] = memoryManager->allocate<WeakSetCtor>(this);
    jsObjects[WeakSetProto] = memoryManager->allocate<WeakSetPrototype>();
    static_cast<WeakSetPrototype *>(weakSetPrototype())->init(this, weakSetCtor());

    jsObjects[Set_Ctor] = memoryManager->allocate<SetCtor>(this);
    jsObjects[SetProto] = memoryManager->allocate<SetPrototype>();
    static_cast<SetPrototype *>(setPrototype())->init(this, setCtor());

    //
    // promises
    //

    jsObjects[Promise_Ctor] = memoryManager->allocate<PromiseCtor>(this);
    jsObjects[PromiseProto] = memoryManager->allocate<PromisePrototype>();
    static_cast<PromisePrototype *>(promisePrototype())->init(this, promiseCtor());

    // typed arrays

    jsObjects[SharedArrayBuffer_Ctor] = memoryManager->allocate<SharedArrayBufferCtor>(this);
    jsObjects[SharedArrayBufferProto] = memoryManager->allocate<SharedArrayBufferPrototype>();
    static_cast<SharedArrayBufferPrototype *>(sharedArrayBufferPrototype())->init(this, sharedArrayBufferCtor());

    jsObjects[ArrayBuffer_Ctor] = memoryManager->allocate<ArrayBufferCtor>(this);
    jsObjects[ArrayBufferProto] = memoryManager->allocate<ArrayBufferPrototype>();
    static_cast<ArrayBufferPrototype *>(arrayBufferPrototype())->init(this, arrayBufferCtor());

    jsObjects[DataView_Ctor] = memoryManager->allocate<DataViewCtor>(this);
    jsObjects[DataViewProto] = memoryManager->allocate<DataViewPrototype>();
    static_cast<DataViewPrototype *>(dataViewPrototype())->init(this, dataViewCtor());
    jsObjects[ValueTypeProto] = (Heap::Base *) nullptr;
    jsObjects[SignalHandlerProto] = (Heap::Base *) nullptr;
    jsObjects[TypeWrapperProto] = (Heap::Base *) nullptr;

    jsObjects[IntrinsicTypedArray_Ctor] = memoryManager->allocate<IntrinsicTypedArrayCtor>(this);
    jsObjects[IntrinsicTypedArrayProto] = memoryManager->allocate<IntrinsicTypedArrayPrototype>();
    static_cast<IntrinsicTypedArrayPrototype *>(intrinsicTypedArrayPrototype())
            ->init(this, static_cast<IntrinsicTypedArrayCtor *>(intrinsicTypedArrayCtor()));

    for (int i = 0; i < NTypedArrayTypes; ++i) {
        static_cast<Value &>(typedArrayCtors[i]) = memoryManager->allocate<TypedArrayCtor>(this, Heap::TypedArray::Type(i));
        static_cast<Value &>(typedArrayPrototype[i]) = memoryManager->allocate<TypedArrayPrototype>(Heap::TypedArray::Type(i));
        typedArrayPrototype[i].as<TypedArrayPrototype>()->init(this, static_cast<TypedArrayCtor *>(typedArrayCtors[i].as<Object>()));
    }

    //
    // set up the global object
    //
    rootContext()->d()->activation.set(scope.engine, globalObject->d());
    Q_ASSERT(globalObject->d()->vtable());

    globalObject->defineDefaultProperty(QStringLiteral("Object"), *objectCtor());
    globalObject->defineDefaultProperty(QStringLiteral("String"), *stringCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Symbol"), *symbolCtor());
    FunctionObject *numberObject = numberCtor();
    globalObject->defineDefaultProperty(QStringLiteral("Number"), *numberObject);
    globalObject->defineDefaultProperty(QStringLiteral("Boolean"), *booleanCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Array"), *arrayCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Function"), *functionCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Date"), *dateCtor());
    globalObject->defineDefaultProperty(QStringLiteral("RegExp"), *regExpCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Error"), *errorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("EvalError"), *evalErrorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("RangeError"), *rangeErrorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("ReferenceError"), *referenceErrorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("SyntaxError"), *syntaxErrorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("TypeError"), *typeErrorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("URIError"), *uRIErrorCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Promise"), *promiseCtor());
    globalObject->defineDefaultProperty(QStringLiteral("URL"), *urlCtor());
    globalObject->defineDefaultProperty(QStringLiteral("URLSearchParams"), *urlSearchParamsCtor());

    globalObject->defineDefaultProperty(QStringLiteral("SharedArrayBuffer"), *sharedArrayBufferCtor());
    globalObject->defineDefaultProperty(QStringLiteral("ArrayBuffer"), *arrayBufferCtor());
    globalObject->defineDefaultProperty(QStringLiteral("DataView"), *dataViewCtor());
    globalObject->defineDefaultProperty(QStringLiteral("WeakSet"), *weakSetCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Set"), *setCtor());
    globalObject->defineDefaultProperty(QStringLiteral("WeakMap"), *weakMapCtor());
    globalObject->defineDefaultProperty(QStringLiteral("Map"), *mapCtor());

    for (int i = 0; i < NTypedArrayTypes; ++i)
        globalObject->defineDefaultProperty((str = typedArrayCtors[i].as<FunctionObject>()->name()), typedArrayCtors[i]);
#endif

#if defined(BFREE_GUEST_FIXED_STACK)
    FunctionObject *numberObject = jsObjects[Number_Ctor].isManaged() ? numberCtor() : nullptr;
#else
    FunctionObject *numberObject = numberCtor();
#endif"""

changed = False
if nic_new.split("protoHeap")[0] in te:
    print("[v224] newInternalClass proto heap already patched")
elif nic_old not in te:
    raise SystemExit("[v224] newInternalClass anchor missing")
else:
    te = te.replace(nic_old, nic_new, 1)
    changed = True
    print("[v224] newInternalClass proto heap path")

if "iterator_ic_enter" in te:
    print("[v224] iterator tail guest already patched")
elif tail_old not in te:
    raise SystemExit("[v224] iterator tail anchor missing")
else:
    te = te.replace(tail_old, tail_new, 1)
    changed = True
    print("[v224] iterator + partial proto/global guest path")

if changed:
    eng.write_text(te)
print("[v224] done")
