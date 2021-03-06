#include <limits>
#include <algorithm>
#include <math.h>
#include "v8-internal.h"

namespace v8 {
using namespace internal;

//////////////////////////////////////////////////////////////////////////////
//// TryCatch class
namespace internal {
  struct ExceptionHandlerChain {
    TryCatch *catcher;
    ExceptionHandlerChain *next;
  };
  static ExceptionHandlerChain *gExnChain = 0;
}
void TryCatch::ReportError(JSContext *ctx, const char *message, JSErrorReport *report) {
  if (!gExnChain) {
    fprintf(stderr, "%s:%u:%s\n",
            report->filename ? report->filename : "<no filename>",
            (unsigned int) report->lineno,
            message);
    return;
  }
  // TODO: figure out if we care about other types of warnings
  if (!JSREPORT_IS_EXCEPTION(report->flags))
    return;

  TryCatch *current = gExnChain->catcher;
  JSExceptionState *state = JS_SaveExceptionState(cx());
  if (current->mCaptureMessage) {
    v8::Message m(message, report);
    current->mMessage = Persistent<v8::Message>::New(&m);
  }
  JS_RestoreExceptionState(cx(), state);
}

void TryCatch::CheckForException() {
  if (!JS_IsExceptionPending(cx())) {
    return;
  }

  TryCatch *current = gExnChain->catcher;
  current->mHasCaught = true;

  Value exn;
  if (!JS_GetPendingException(cx(), &exn.native())) {
    // TODO: log warning?
    return;
  }
  current->mException = Persistent<Value>::New(&exn);
  if (current->mCaptureMessage) {
    JS_ReportPendingException(cx());
  }
}


TryCatch::TryCatch() :
  mHasCaught(false),
  mCaptureMessage(true),
  mRethrown(false)
{
  ExceptionHandlerChain *link = new ExceptionHandlerChain;
  link->catcher = this;
  link->next = gExnChain;
  gExnChain = link;
}

TryCatch::~TryCatch() {
  ExceptionHandlerChain *link = gExnChain;
  JS_ASSERT(link->catcher == this);
  gExnChain = gExnChain->next;
  delete link;

  if (mRethrown) {
    JS_SetPendingException(cx(), mException->native());
    CheckForException();
  } else if (mHasCaught) {
    JS_ClearPendingException(cx());
  }

  Reset();
}

Handle<Value> TryCatch::ReThrow() {
  if (!HasCaught()) {
    return Handle<Value>();
  }
  mRethrown = true;
  return Undefined();
}

Local<Value> TryCatch::StackTrace() const {
  Handle<Object> obj(mException.As<Object>());
  if (obj.IsEmpty())
    return Local<Value>();
  return obj->Get(String::New("stack"));
}

Local<v8::Message> TryCatch::Message() const {
  return Local<v8::Message>::New(*mMessage);
}

void TryCatch::Reset() {
  mException.Dispose();
  mMessage.Dispose();

  mHasCaught = false;
}

void TryCatch::SetVerbose(bool value) {
  // TODO: figure out what to do with this
}

void TryCatch::SetCaptureMessage(bool value) {
  mCaptureMessage = value;
}

//////////////////////////////////////////////////////////////////////////////
//// Context class

namespace internal {
  struct ContextChain {
    Context* ctx;
    ContextChain *next;
  };
  static ContextChain *gContextChain = 0;
}

Context::Context(JSObject *global) :
  internal::SecretObject<internal::GCReference>(global)
{}

Local<Context> Context::GetEntered() {
  return Local<Context>::New(gContextChain->ctx);
}

Local<Context> Context::GetCurrent() {
  // XXX: This is probably not right
  return Local<Context>::New(gContextChain->ctx);
}

void Context::Enter() {
  ContextChain *link = new ContextChain;
  link->next = gContextChain;
  link->ctx = this;
  JS_SetGlobalObject(cx(), InternalObject());
  gContextChain = link;
}

void Context::Exit() {
  ContextChain *link = gContextChain;
  gContextChain = gContextChain->next;
  delete link;
  JSObject *global = gContextChain ? gContextChain->ctx->InternalObject() : NULL;
  JS_SetGlobalObject(cx(), global);
}

Persistent<Context> Context::New(
      ExtensionConfiguration* config,
      Handle<ObjectTemplate> global_template,
      Handle<Value> global_object) {
  if (!global_object.IsEmpty())
    UNIMPLEMENTEDAPI(Persistent<Context>());
  JSObject *global = JS_NewGlobalObject(cx(), &global_class);

  JS_InitStandardClasses(cx(), global);
  if (!global_template.IsEmpty()) {
    JS_SetPrototype(cx(), global, JSVAL_TO_OBJECT(global_template->native()));
  }

  return Persistent<Context>(new Context(global));
}

//////////////////////////////////////////////////////////////////////////////
//// Resource Constraints

ResourceConstraints::ResourceConstraints() :
  mMaxYoungSpaceSize(0),
  mMaxOldSpaceSize(0),
  mMaxExecutableSize(0),
  mStackLimit(NULL)
{
}

bool SetResourceConstraints(ResourceConstraints *constraints) {
  // TODO: Pull the relevent information out that applies to SM and set some
  //       globals that would be used.
  return true;
}

//////////////////////////////////////////////////////////////////////////////
//// Value class

JS_STATIC_ASSERT(sizeof(Value) == sizeof(GCReference));

Local<Boolean> Value::ToBoolean() const {
  JSBool b;
  if (!JS_ValueToBoolean(cx(), mVal, &b))
    return Local<Boolean>();

  Boolean value(b);
  return Local<Boolean>::New(&value);
}

Local<Number> Value::ToNumber() const {
  return Number::New(this->NumberValue());
}

Local<String> Value::ToString() const {
  // TODO Allocate this in a way that doesn't leak
  JSString *str(JS_ValueToString(cx(), mVal));
  String s(str);
  return Local<String>::New(&s);
}

Local<Uint32> Value::ToUint32() const {
  if (!IsNumber()) {
    return Local<Uint32>();
  }

  JSUint32 i = 0;
  (void) JS_ValueToECMAUint32(cx(), mVal, &i);
  Uint32 v(i);
  return Local<Uint32>::New(&v);
}

Local<Int32> Value::ToInt32() const {
  JSInt32 i = this->Int32Value();
  Int32 v(i);
  return Local<Int32>::New(&v);
}

Local<Object>
Value::ToObject() const
{
  if (JSVAL_IS_OBJECT(mVal)) {
    Value* val = const_cast<Value*>(this);
    return Local<Object>::New(reinterpret_cast<Object*>(val));
  }

  return Local<Object>();
}

bool Value::IsFunction() const {
  if (!IsObject())
    return false;
  JSObject *obj = JSVAL_TO_OBJECT(mVal);
  return JS_ObjectIsFunction(cx(), obj);
}

bool Value::IsArray() const {
  if (!IsObject())
    return false;
  JSObject *obj = JSVAL_TO_OBJECT(mVal);
  return JS_IsArrayObject(cx(), obj);
}

bool Value::IsDate() const {
  if (!IsObject())
    return false;
  JSObject *obj = JSVAL_TO_OBJECT(mVal);
  return JS_ObjectIsDate(cx(), obj);
}

bool Value::IsUint32() const {
  if (!this->IsNumber())
    return false;

  double d = this->NumberValue();
  return d >= 0 &&
         (this->IsInt32() || (d <= std::numeric_limits<JSUint32>::max() && ceil(d) == floor(d)));
}

bool
Value::BooleanValue() const
{
  JSBool b;
  JS_ValueToBoolean(cx(), mVal, &b);
  return b == JS_TRUE;
}

double
Value::NumberValue() const
{
  double d;
  JS_ValueToNumber(cx(), mVal, &d);
  return d;
}

JSInt64
Value::IntegerValue() const
{
  UNIMPLEMENTEDAPI(0);
}

JSUint32
Value::Uint32Value() const
{
  Local<Uint32> n = ToUint32();
  if (n.IsEmpty())
    return 0;
  return n->Value();
}

JSInt32
Value::Int32Value() const
{
  JSInt32 i = 0;
  JS_ValueToECMAInt32(cx(), mVal, &i);
  return i;
}

bool
Value::Equals(Handle<Value> other) const
{
  JSBool equal;
  JS_LooselyEqual(cx(), mVal, other->native(), &equal);
  return equal == JS_TRUE;
}

bool
Value::StrictEquals(Handle<Value> other) const
{
  JSBool equal;
  // XXX: check for error. This can fail if they are ropes that fail to turn into strings
  (void) JS_StrictlyEqual(cx(), mVal, other->native(), &equal);
  return equal == JS_TRUE;
}

//////////////////////////////////////////////////////////////////////////////
//// Boolean class

JS_STATIC_ASSERT(sizeof(Boolean) == sizeof(GCReference));

Handle<Boolean> Boolean::New(bool value) {
  static Boolean sTrue(true);
  static Boolean sFalse(false);
  return value ? &sTrue : &sFalse;
}

//////////////////////////////////////////////////////////////////////////////
//// Number class

JS_STATIC_ASSERT(sizeof(Number) == sizeof(GCReference));

Local<Number> Number::New(double d) {
  jsval val;
  if (!JS_NewNumberValue(cx(), d, &val))
    return Local<Number>();

  Number n(val);
  return Local<Number>::New(&n);
}

double Number::Value() const {
  double d;
  JS_ValueToNumber(cx(), mVal, &d);
  return d;
}


//////////////////////////////////////////////////////////////////////////////
//// Integer class

JS_STATIC_ASSERT(sizeof(Integer) == sizeof(GCReference));

Local<Integer> Integer::New(JSInt32 value) {
  jsval val = INT_TO_JSVAL(value);
  Integer i(val);
  return Local<Integer>::New(&i);
}
Local<Integer> Integer::NewFromUnsigned(JSUint32 value) {
  jsval val = UINT_TO_JSVAL(value);
  Integer i(val);
  return Local<Integer>::New(&i);
}
JSInt64 Integer::Value() const {
  // XXX We should keep track of mIsDouble or something, but that wasn't working...
  if (JSVAL_IS_INT(mVal)) {
    return static_cast<JSInt64>(JSVAL_TO_INT(mVal));
  }
  else {
    return static_cast<JSInt64>(JSVAL_TO_DOUBLE(mVal));
  }
}

////////////////////////////////////////////////////////////////////////////////
//// Int32 class

JS_STATIC_ASSERT(sizeof(Int32) == sizeof(GCReference));

JSInt32 Int32::Value() {
  return JSVAL_TO_INT(mVal);
}

////////////////////////////////////////////////////////////////////////////////
//// Uint32 class

JS_STATIC_ASSERT(sizeof(Uint32) == sizeof(GCReference));

JSUint32 Uint32::Value() {
  if (JSVAL_IS_INT(mVal)) {
    return static_cast<JSUint32>(JSVAL_TO_INT(mVal));
  }
  return static_cast<JSUint32>(JSVAL_TO_DOUBLE(mVal));
}


//////////////////////////////////////////////////////////////////////////////
//// Date class

JS_STATIC_ASSERT(sizeof(Date) == sizeof(GCReference));

Local<Value> Date::New(double time) {
  // We floor the value since anything after the decimal is not used.
  // This keeps us from having to specialize Value::NumberValue.
  JSObject *obj = JS_NewDateObjectMsec(cx(), floor(time));
  Value v(OBJECT_TO_JSVAL(obj));
  return Local<Value>::New(&v);
}


//////////////////////////////////////////////////////////////////////////////
//// Primitives & basic values

Handle<Primitive> Undefined() {
  static Primitive p(JSVAL_VOID);
  return Handle<Primitive>(&p);
}

Handle<Primitive> Null() {
  static Primitive p(JSVAL_NULL);
  return Handle<Primitive>(&p);
}

Handle<Boolean> True() {
  return Boolean::New(true);
}

Handle<Boolean> False() {
  return Boolean::New(false);
}



//////////////////////////////////////////////////////////////////////////////
//// ScriptOrigin class

ScriptOrigin::ScriptOrigin(Handle<Value> resourceName,
                           Handle<Integer> resourceLineOffset,
                           Handle<Integer> resourceColumnOffset) :
  mResourceName(resourceName),
  mResourceLineOffset(resourceLineOffset),
  mResourceColumnOffset(resourceColumnOffset)
{
}
Handle<Value> ScriptOrigin::ResourceName() const {
  return mResourceName;
}
Handle<Integer> ScriptOrigin::ResourceLineOffset() const {
  return mResourceLineOffset;
}
Handle<Integer> ScriptOrigin::ResourceColumnOffset() const {
  return mResourceColumnOffset;
}


//////////////////////////////////////////////////////////////////////////////
//// ScriptData class
ScriptData::~ScriptData() {
  if (mScript)
    JS_RemoveObjectRoot(cx(), &mScript);
  if (mXdr)
    JS_XDRDestroy(mXdr);
}

void ScriptData::SerializeScriptObject(JSObject *scriptObj) {
  mXdr = JS_XDRNewMem(cx(), JSXDR_ENCODE);
  if (!mXdr)
    return;

  if (!JS_XDRScriptObject(mXdr, &scriptObj)) {
    JS_XDRDestroy(mXdr);
    mXdr = NULL;
    return;
  }

  uint32 length;
  void *buf = JS_XDRMemGetData(mXdr, &length);
  if (!buf) {
    JS_XDRDestroy(mXdr);
    mXdr = NULL;
    return;
  }

  mData = static_cast<const char*>(buf);
  mLen = length;
  mError = false;
}

ScriptData* ScriptData::PreCompile(const char* input, int length) {
  JSObject *global = JS_GetGlobalObject(cx());
  ScriptData *sd = new ScriptData();
  if (!sd)
    return NULL;

  JSObject *scriptObj = JS_CompileScript(cx(), global,
                                         input, length, NULL, 0);
  if (!scriptObj)
    return sd;

  if (sd)
    sd->SerializeScriptObject(scriptObj);
  return sd;
}

ScriptData* ScriptData::PreCompile(Handle<String> source) {
  JS::Anchor<JSString*> anchor(JSVAL_TO_STRING(source->native()));
  const jschar* chars;
  size_t len;
  chars = JS_GetStringCharsAndLength(cx(),
                                     anchor.get(), &len);
  JSObject *global = JS_GetGlobalObject(cx());
  ScriptData *sd = new ScriptData();
  if (!sd)
    return NULL;

  JSObject *scriptObj = JS_CompileUCScript(cx(), global,
                                           chars, len, NULL, 0);
  if (!scriptObj)
    return sd;

  if (sd)
    sd->SerializeScriptObject(scriptObj);
  return sd;
}

ScriptData* ScriptData::New(const char* aData, int aLength) {
  ScriptData *sd = new ScriptData();
  if (!sd)
    return NULL;

  sd->mScript = sd->GenerateScriptObject((void *)aData, aLength);
  sd->mError = !sd->mScript;

  if (!sd->mScript)
    return sd;

  JS_AddObjectRoot(cx(), &sd->mScript);
  return sd;
}

int ScriptData::Length() {
  if (!mData && mScript)
    SerializeScriptObject(mScript);
  return mLen;
}

const char* ScriptData::Data() {
  if (!mData && mScript)
    SerializeScriptObject(mScript);
  return mData;
}

bool ScriptData::HasError() {
  return mError;
}

JSObject* ScriptData::ScriptObject() {
  return mScript;
}

JSObject* ScriptData::GenerateScriptObject(void *aData, int aLen) {
  mXdr = JS_XDRNewMem(cx(), JSXDR_DECODE);
  if (!mXdr)
    return NULL;

  // Caller reports error via HasError if the script fails to deserialize
  JSErrorReporter older = JS_SetErrorReporter(cx(), NULL);
  JS_XDRMemSetData(mXdr, aData, aLen);

  JSObject *scriptObj;
  JS_XDRScriptObject(mXdr, &scriptObj);

  JS_XDRMemSetData(mXdr, NULL, 0);
  JS_SetErrorReporter(cx(), older);
  JS_XDRDestroy(mXdr);
  mXdr = NULL;

  return scriptObj;
}

//////////////////////////////////////////////////////////////////////////////
//// Script class

JS_STATIC_ASSERT(sizeof(Script) == sizeof(GCReference));

Script::Script(JSObject *s)
{
  mVal = OBJECT_TO_JSVAL(s);
}

Script::operator JSObject *() {
  return JSVAL_TO_OBJECT(mVal);
}

Handle<Object> Script::InternalObject() {
  Object o(JSVAL_TO_OBJECT(mVal));
  return Local<Object>::New(&o);
}

Local<Script> Script::Create(Handle<String> source, ScriptOrigin *origin, ScriptData *preData, Handle<String> scriptData, bool bindToCurrentContext) {
  JSObject* s = NULL;

  if (preData)
    s = preData->ScriptObject();

  if (!s) {
    JS::Anchor<JSString*> anchor(JSVAL_TO_STRING(source->native()));
    const jschar* chars;
    size_t len;
    chars = JS_GetStringCharsAndLength(cx(),
                                       anchor.get(), &len);

    s = JS_CompileUCScript(cx(), NULL,
                           chars, len, NULL, 0);
  }

  Script script(s);
  if (bindToCurrentContext) {
    script.InternalObject()->Set(String::New("global"), Context::GetCurrent()->Global());
  }
  return Local<Script>::New(&script);
}

Local<Script> Script::New(Handle<String> source, ScriptOrigin *origin,
                          ScriptData *preData, Handle<String> scriptData) {
  return Create(source, origin, preData, scriptData, false);
}

Local<Script> Script::New(Handle<String> source, Handle<Value> fileName) {
  ScriptOrigin origin(fileName);
  return New(source, &origin);
}

Local<Script> Script::Compile(Handle<String> source, ScriptOrigin *origin,
                              ScriptData *preData, Handle<String> scriptData) {
  return Create(source, origin, preData, scriptData, true);
}

Local<Script> Script::Compile(Handle<String> source, Handle<Value> fileName,
                              Handle<String> scriptData) {
  ScriptOrigin origin(fileName);
  return Compile(source, &origin);
}

Local<Value> Script::Run() {
  Handle<Value> boundGlobalValue = InternalObject()->Get(String::New("global"));
  Handle<Object> global;
  if (boundGlobalValue.IsEmpty()) {
    global = Context::GetCurrent()->Global();
  } else {
    global = boundGlobalValue.As<Object>();
  }
  jsval js_retval;
  if (!JS_ExecuteScript(cx(), **global,
                        *this, &js_retval)) {
    TryCatch::CheckForException();
    return Local<Value>();
  }
  Value v(js_retval);
  return Local<Value>::New(&v);
}

Local<Value> Script::Id() {
  Value v(mVal);
  return Local<Value>::New(&v);
}

//////////////////////////////////////////////////////////////////////////////
//// Message class

JS_STATIC_ASSERT(sizeof(Message) == sizeof(GCReference));

namespace {
  JSClass message_class = {
    "v8::Message", JSCLASS_HAS_PRIVATE,
    JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, JS_FinalizeStub,
    JSCLASS_NO_OPTIONAL_MEMBERS
  };
}

Message::Message(const char *message, JSErrorReport *report) :
  SecretObject<internal::GCReference>(JS_NewObject(cx(), &message_class, NULL, NULL))
{
  const char *filename = report->filename ? report->filename : "";
  const char *linebuf = report->linebuf ? report->linebuf : "";
  Object &o = InternalObject();
  o.Set(String::New("message"), String::New(message));
  o.Set(String::New("filename"), String::New(filename));
  o.Set(String::New("lineNumber"), Integer::New(report->lineno));
  o.Set(String::New("line"), String::New(linebuf));
}

Local<String> Message::Get() const {
  return InternalObject().Get(String::New("message"))->ToString();
}

Local<String> Message::GetSourceLine() const {
  return InternalObject().Get(String::New("line"))->ToString();
}

Handle<Value> Message::GetScriptResourceName() const {
  return InternalObject().Get(String::New("filename"));
}

Handle<Value> Message::GetScriptData() const {
  UNIMPLEMENTEDAPI(NULL);
}

Handle<StackTrace> Message::GetStackTrace() const {
  UNIMPLEMENTEDAPI(NULL);
}

int Message::GetLineNumber() const {
  return InternalObject().Get(String::New("lineNumber"))->Int32Value();
}

int Message::GetStartPosition() const {
  UNIMPLEMENTEDAPI(0);
}

int Message::GetEndPosition() const {
  UNIMPLEMENTEDAPI(0);
}

int Message::GetStartColumn() const {
  UNIMPLEMENTEDAPI(kNoColumnInfo);
}
int Message::GetEndColumn() const {
  UNIMPLEMENTEDAPI(kNoColumnInfo);
}

void Message::PrintCurrentStackTrace(FILE*) {
  UNIMPLEMENTEDAPI();
}

//////////////////////////////////////////////////////////////////////////////
//// Arguments class

Arguments::Arguments(JSContext* cx, JSObject* thisObj, int nargs, jsval* vp, Handle<Value> data) :
  mCtx(cx), mValues(vp), mThis(thisObj), mLength(nargs), mData(Local<Value>::New(*data))
{
}

Local<Value> Arguments::operator[](int i) const {
  if (i < 0 || i >= mLength)
    return Local<Value>();
  Value v(mValues[i]);
  return Local<Value>::New(&v);
}

Local<Function> Arguments::Callee() const {
  jsval callee = JS_CALLEE(mCtx, mValues);
  Function f(JSVAL_TO_OBJECT(callee));
  return Local<Function>::New(&f);
}

bool Arguments::IsConstructCall() const {
  return JS_IsConstructing(mCtx, mValues);
}

AccessorInfo::AccessorInfo(Handle<Value> data, JSObject *obj) :
  mData(data), mObj(obj)
{}

Local<Object> AccessorInfo::This() const {
  Object o(mObj);
  return Local<Object>::New(&o);
}

const AccessorInfo
AccessorInfo::MakeAccessorInfo(Handle<Value> data,
                               JSObject* obj)
{
  return AccessorInfo(data, obj);
}

}
