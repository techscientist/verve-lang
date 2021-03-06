#include "type_checker.h"

#include <cstdarg>
#include <cstdio>

namespace Verve {

class TypeError : public std::exception {
public:
  TypeError(Loc loc, const char *format, ...) :
    m_loc(loc)
  {
    va_list args1;
    va_list args2;
    va_start(args1, format);
    va_start(args2, format);
    auto length = vsnprintf(NULL, 0, format, args1);
    m_msg = (char *)malloc(length + 1);
    vsnprintf(m_msg, length + 1, format, args2);
    va_end(args1);
    va_end(args2);
  }

  ~TypeError() {
    free(m_msg);
  }

  virtual const char *what() const noexcept {
    return m_msg;
  }

  Loc &loc() {
    return m_loc;
  }

private:
  Loc m_loc;
  char *m_msg;
};

static Type *simplifyType(Type *type, EnvPtr env) {
  if (auto gt = dynamic_cast<GenericType *>(type)) {
    auto t = env->get(gt->typeName);
    if (t != type) {
      return simplifyType(t, env);
    }
  } else if (auto dti = dynamic_cast<DataTypeInstance *>(type)) {
    dti->dataType = simplifyType(dti->dataType, env);
    for (unsigned i = 0; i < dti->types.size(); i++) {
      dti->types[i] = simplifyType(dti->types[i], env);
    }
  } else if (auto interface = dynamic_cast<TypeInterface *>(type)) {
    auto t = env->get(interface->genericTypeName);
    if (t && t != type && !dynamic_cast<GenericType *>(t)) {
      return simplifyType(t, env);
    }
  }
  return type;
}

static void loadFnGenerics(TypeFunction *fnType, EnvPtr env) {
  if (fnType->interface) {
    env->types[fnType->interface->genericTypeName] = fnType->interface;
  }

  for (auto generic : fnType->generics) {
    env->types[generic] = new GenericType(generic);
  }
}

static Type *typeCheckArguments(std::vector<AST::NodePtr> &arguments, TypeFunction *fnType, EnvPtr env, Loc &loc) {
  if (arguments.size() != fnType->types.size()) {
    throw TypeError(loc, "Wrong number of arguments for function call");
  }

  loadFnGenerics(fnType, env);

  for (unsigned i = 0; i < fnType->types.size(); i++) {
    auto arg = arguments[i];

    auto expected = simplifyType(fnType->types[i], env);
    auto actual = simplifyType(arg->typeof(env), env);

    if (!actual) {
      throw TypeError(arg->loc, "Can't find type information for call argument #%d", i + 1);
    } else if (!expected->accepts(actual, env)) {
      throw TypeError(arg->loc, "Expected `%s` but got `%s` on arg #%d for function `%s`",
          expected->toString().c_str(),
          actual->toString().c_str(),
          i + 1,
          fnType->name.c_str());
    }
  }

  if (auto et = dynamic_cast<EnumType *>(fnType->returnType)) {
    if (et->generics.size()) {
      auto returnType = new DataTypeInstance();
      returnType->dataType = et;
      for (auto t : et->generics) {
        returnType->types.push_back(env->get(t));
      }
      return returnType;
    }
  }

  return simplifyType(fnType->returnType, env);
}

Type *TypeChecker::typeof(AST::NodePtr node, EnvPtr env, Lexer &lexer) {
  try {
    auto type = node->typeof(env);
    if (!type) {
      throw TypeError(node->loc, "Unknown type for expression");
    }
    return type;
  } catch (TypeError &ex) {
    fprintf(stderr, "Type Error: %s\n", ex.what());
    lexer.printSource(ex.loc());
    throw std::runtime_error("type error");
  }
}

Type *AST::String::typeof(EnvPtr env) {
  return env->get("string");
}

Type *AST::BinaryOperation::typeof(EnvPtr env) {
  auto intType = env->get("int");
  AST::NodePtr failed = nullptr;
  Verve::Type *failedType = nullptr;
  if (!intType->accepts((failedType = lhs->typeof(env)), env)) {
    failed = lhs;
  } else if (!intType->accepts((failedType = rhs->typeof(env)), env)) {
    failed = rhs;
  }

  if (failed != nullptr) {
    throw TypeError(failed->loc, "Binary operations only accept `int`, but found `%s`", failedType->toString().c_str());
  }

  return env->get("int");
}

Type *AST::UnaryOperation::typeof(EnvPtr env) {
  return env->get("int");
}

Type *AST::Number::typeof(EnvPtr env) {
  return env->get(isFloat ? "float" : "int");
}

Type *AST::Identifier::typeof(EnvPtr env) {
  return env->get(name);
}

Type *AST::Function::typeof(EnvPtr env) {
  auto type = env->get(name);
  auto fnType = dynamic_cast<TypeFunction *>(type);
  if (!fnType) {
    throw TypeError(this->loc, "Couldn't find type information for function `%s`", name.c_str());
  }

  loadFnGenerics(fnType, env);

  auto expected = simplifyType(fnType->returnType, env);
  auto actual = simplifyType(body->typeof(env), env);

  if (!expected->accepts(actual, env)) {
    throw TypeError(body->loc, "Invalid return type for function: expected `%s` but got `%s`", expected->toString().c_str(), actual->toString().c_str());
  }

  auto t = new TypeFunction(*fnType);
  t->returnType = actual;

  return t;
}

Type *AST::Block::typeof(EnvPtr env) {
  if (nodes.empty()) {
    return env->get("void");
  } else {
    ::Verve::Type *t;
    for (auto node : nodes) {
      env = this->env ?: env;
      t = node->typeof(env->create());
    }
    return t;
  }
}

Type *AST::Call::typeof(EnvPtr env) {
  auto calleeType = callee->typeof(env);
  TypeFunction *fnType;

  if(!(fnType = dynamic_cast<TypeFunction *>(calleeType))) {
    throw TypeError(loc, "Can't find type information for function call");
  }

  auto returnType = typeCheckArguments(arguments, fnType, env, loc);

  if (fnType->interface) {
    auto ident = AST::asIdentifier(callee);
    auto name = ident->name + env->types[fnType->interface->genericTypeName]->toString();
    if (env->get(name)) {
      ident->name = name;
    }
  }

  return simplifyType(returnType, env);
}

Type *AST::If::typeof(EnvPtr env) {
  auto iffType = ifBody->typeof(env);
  if (elseBody) {
    auto elseType = elseBody->typeof(env);
    // return the least specific type
    if (iffType->accepts(elseType, env)) {
      return iffType;
    } else if (elseType->accepts(iffType, env)) {
      return elseType;
    } else {
      throw TypeError(loc, "`if` and `else` branches evaluate to different types");
    }
  }
  return iffType;
}

Type *AST::List::typeof(EnvPtr env) {
  auto dataType = dynamic_cast<EnumType *>(env->get("list"));
  ::Verve::Type *t = nullptr;
  for (auto item : items) {
    auto type = simplifyType(item->typeof(env), env);

    if (!t) {
      t = type;
    } else {
      if (!t->accepts(type, env)) {
        if (type->accepts(t, env)) {
          t = type;
        } else {
          throw TypeError(item->loc, "Lists can't have mixed types: found an element of type `%s` when elements' inferred type was `%s`",
              type->toString().c_str(),
              t->toString().c_str());
        }
      }
    }
  }

  auto dti = new DataTypeInstance();
  dti->dataType = dataType;
  dti->types.push_back(t);
  return dti;
}

Type *AST::Match::typeof(EnvPtr env) {
  if (!cases.size()) {
    throw TypeError(loc, "Cannot have `match` expression with no cases");
  }

  ::Verve::Type *t = nullptr;
  for (auto kase : cases) {
    auto type = simplifyType(kase->typeof(env), env);
    if (!t) {
      t = type;
    } else {
      if (!t->accepts(type, env)) {
        if (type->accepts(t, env)) {
          t = type;
        } else {
          throw TypeError(kase->loc, "Match can't have mixed types on its cases: found a case with type `%s` when previous cases' inferred type was `%s`",
              type->toString().c_str(),
              t->toString().c_str());
        }
      }
    }
  }
  return t;
}

Type *AST::Case::typeof(EnvPtr env) {
  if (!env->get(pattern->constructorName)) {
    throw TypeError(pattern->loc, "Unknown constructor `%s` on pattern match", pattern->constructorName.c_str());
  }
  return body->typeof(env);
}

Type *AST::Let::typeof(__unused EnvPtr _) {
  for (auto assignment : assignments) {
    assert(assignment->typeof(env));
  }
  return block->typeof(env);
}

Type *AST::Assignment::typeof(EnvPtr env) {
  auto valueType = value->typeof(env);
  if (left->type == AST::Type::Pattern) {
    auto pattern = AST::asPattern(left);
    auto patternType = env->get(pattern->constructorName);
    if (!valueType->accepts(patternType, env)) {
      throw TypeError(pattern->loc, "Trying to pattern match value of type `%s` with constructor `%s`", valueType->toString().c_str(), patternType->toString().c_str());
    }
  } else {
    assert(left->type == AST::Type::Identifier);
  }
  return valueType;
}

Type *AST::Constructor::typeof(EnvPtr env) {
  auto type = env->get(name);
  auto ctorType = dynamic_cast<TypeConstructor *>(type);
  if (!ctorType) {
    throw TypeError(loc, "Undefined constructor: `%s`", name.c_str());
  }
  return typeCheckArguments(arguments, ctorType->type, env, loc);
}

}
