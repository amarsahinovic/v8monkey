/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

#include "v8api_test_harness.h"

////////////////////////////////////////////////////////////////////////////////
//// Tests

Handle<Value> AddOne(const Arguments& args) {
  do_check_eq(args.Length(), 1);
  Local<Value> v = args[0];
  do_check_true(v->IsNumber());
  Local<Number> n = v->ToNumber();
  return Number::New(n->Value() + 1.0);
}

void
test_BasicCall()
{
  HandleScope handle_scope;
  Handle<ObjectTemplate> templ = ObjectTemplate::New();
  Handle<FunctionTemplate> fnT = v8::FunctionTemplate::New(AddOne);
  templ->Set("AddOne", fnT);

  Persistent<Context> context = Context::New(NULL, templ);
  Local<Value> addone = context->Global()->Get(String::New("AddOne"));
  do_check_true(!addone.IsEmpty());
  do_check_true(!addone->IsUndefined());
  do_check_true(addone->IsObject());
  do_check_true(addone->IsFunction());
  Local<Function> fn = addone.As<Function>();
  do_check_eq(fn, fnT->GetFunction());
  Local<Number> n = Number::New(0.5);
  Handle<Value> args[] = { n };
  Local<Value> v = fn->Call(context->Global(), 1, args);
  do_check_true(!v.IsEmpty());
  do_check_true(v->IsNumber());
  Local<Number> n2 = v->ToNumber();
  do_check_eq(n2->Value(), 1.5);
}

Handle<Value> InvokeCallback(const Arguments& args) {
  return v8::Undefined();
}

Handle<Value> InstanceAccessorCallback(Local<String> property, const AccessorInfo &info) {
  return v8::Undefined();
}

void
test_V8DocExample()
{
  HandleScope handle_scope;
  Persistent<Context> context = Context::New();
  Context::Scope context_scope(context);

  v8::Local<v8::FunctionTemplate> t = v8::FunctionTemplate::New();
  t->Set("func_property", v8::Number::New(1));

  v8::Local<v8::Template> proto_t = t->PrototypeTemplate();
  proto_t->Set("proto_method", v8::FunctionTemplate::New(InvokeCallback));
  proto_t->Set("proto_const", v8::Number::New(2));

  v8::Local<v8::ObjectTemplate> instance_t = t->InstanceTemplate();
  instance_t->SetAccessor(String::New("instance_accessor"), InstanceAccessorCallback);
  instance_t->Set("instance_property", Number::New(3));

  v8::Local<v8::Function> function = t->GetFunction();
  v8::Local<v8::Object> instance = function->NewInstance();
  // TODO: test that we created the objects we were supposed to
}

static
Handle<Value>
NodeFailureCallback(const Arguments& args)
{
  return True();
}

void
test_NodeFailure()
{
  HandleScope handle_scope;
  Persistent<Context> context = Context::New();
  Context::Scope context_scope(context);

  Local<FunctionTemplate> process_template = FunctionTemplate::New();
  Local<FunctionTemplate> constructor_template =
    Local<FunctionTemplate>::New(process_template);
  constructor_template->SetClassName(String::NewSymbol("EventEmitter"));
  Local<Object> process =
    Local<Object>::New(process_template->GetFunction()->NewInstance());

  bool rc;
  rc = process->Set(String::NewSymbol("version"), String::New("1.0"));
  do_check_true(rc);

  rc = process->Set(String::NewSymbol("binding"),
                    FunctionTemplate::New(NodeFailureCallback)->GetFunction());
  do_check_true(rc);

  rc = process->Set(String::NewSymbol("EventEmitter"),
                    constructor_template->GetFunction());
  do_check_true(rc);

  const char* src =
    "(function setup(process) {"
    "   var retval = process.binding();"
    "   return retval;"
    "})";
  Local<Script> script = Script::Compile(String::New(src));
  Local<Value> func_val = script->Run();
  do_check_true(func_val->IsFunction());
  Local<Function> func = Local<Function>::Cast(func_val);
  Local<Object> global = Context::GetCurrent()->Global();
  Local<Value> args[1] = { Local<Value>::New(process) };
  Local<Value> result = func->Call(global, 1, args);
  do_check_eq(result, True());
}

////////////////////////////////////////////////////////////////////////////////
//// Test Harness

Test gTests[] = {
  TEST(test_BasicCall),
  TEST(test_V8DocExample),
  TEST(test_NodeFailure),
};

const char* file = __FILE__;
#define TEST_NAME "FunctionTemplate Class"
#define TEST_FILE file
#include "v8api_test_harness_tail.h"

