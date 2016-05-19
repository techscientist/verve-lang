#include "parser.h"

#include "lexer.h"
#include "token.h"

namespace ceos {
  static Type *GenericInitialValue = (Type *)&GenericInitialValue;

  static std::unordered_map<std::string, TypeChain *> *InterfaceTypeInfo;
  static std::unordered_map<std::string, Type *> *ImplementationTypes;
  static std::string ImplementationTypeName = "";

  std::shared_ptr<AST::Program> Parser::parse(void) {
    m_types["Int"] = new BasicType("Int");
    m_types["Char"] = new BasicType("Char");
    m_types["Float"] = new BasicType("Float");
    m_types["Void"] = new BasicType("Void");
    m_types["List"] = new DataType("List", 1);
    m_types["String"] = new DataTypeInstance((DataType *)m_types["List"], (::ceos::Type *[]){ m_types["Char"] });

    m_ast = std::make_shared<AST::Program>();
    m_ast->loc.start = m_lexer.token()->loc.start;
    m_ast->body = parseBlock(Token::Type::END);
    m_ast->body->needsScope = false;
    return m_ast;
  }

  std::shared_ptr<AST::Block> Parser::parseBlock(Token::Type delim) {
    auto block = std::make_shared<AST::Block>();
    while (m_lexer.token()->type != delim) {
      std::shared_ptr<AST> node = parseFactor();
      if (node) {
        block->nodes.push_back(node);
      }
    }
    block->needsScope = m_scope->isRequired;
    block->capturesScope = m_scope->capturesScope;

    return block;
  }

  std::shared_ptr<AST> Parser::parseFactor(TypeMap *types) {
    switch (m_lexer.token()->type) {
      case Token::Type::NUMBER:
        return parseNumber();
      case Token::Type::ID:
        return parseID(types);
      case Token::Type::STRING:
        return parseString();
      default:
        m_lexer.invalidType();
    }
  }

  std::shared_ptr<AST> Parser::parseIf() {
    auto _if = std::make_shared<AST::If>();

    m_lexer.ensure(Token::Type::L_PAREN);
    _if->condition = parseFactor();
    m_lexer.ensure(Token::Type::R_PAREN);

    if (m_lexer.token()->type == Token::Type::L_BRACE) {
      m_lexer.ensure(Token::Type::L_BRACE);
      _if->ifBody = parseBlock(Token::Type::R_BRACE);
      m_lexer.ensure(Token::Type::R_BRACE);
    } else {
      auto ifBody = std::make_shared<AST::Block>();
      ifBody->nodes.push_back(parseFactor());
      _if->ifBody = ifBody;
    }

    if (m_lexer.token()->type == Token::Type::ID) {
      auto maybeElse = static_cast<Token::ID *>(m_lexer.token());
      if (maybeElse->name == "else") {
        m_lexer.ensure(Token::Type::ID);

        if (m_lexer.token()->type == Token::Type::L_BRACE) {
          m_lexer.ensure(Token::Type::L_BRACE);
          _if->elseBody = parseBlock(Token::Type::R_BRACE);
          m_lexer.ensure(Token::Type::R_BRACE);
        } else {
          auto elseBody = std::make_shared<AST::Block>();
          elseBody->nodes.push_back(parseFactor());
          _if->elseBody = elseBody;
        }
      }
    }

    return _if;
  }

  std::shared_ptr<AST::Number> Parser::parseNumber() {
    auto number = static_cast<Token::Number *>(m_lexer.token(Token::Type::NUMBER));
    auto ast = std::make_shared<AST::Number>(number->value);
    ast->loc = number->loc;
    ast->typeInfo = m_types["Int"];
    return ast;
  }

  std::shared_ptr<AST> Parser::parseID(TypeMap *types) {
    auto id = *static_cast<Token::ID *>(m_lexer.token(Token::Type::ID));

    if (id.name == "if") {
      return parseIf();
    } else if (id.name == "interface") {
      parseInterface();
      return nullptr;
    } else if (id.name == "implementation") {
      return parseImplementation();
    } else if (id.name == "extern") {
      auto name = static_cast<Token::ID *>(m_lexer.token(Token::Type::ID))->name;
      auto typeInfo = parsePrototype();
      typeInfo->external = true;
      m_typeInfo[name] = typeInfo;
      return nullptr;
    } else if (id.name == "virtual") {
      throw std::runtime_error("Virtual functions can only be placed inside interfaces");
    }

    std::shared_ptr<AST> ast, ref;
    if ((ref = m_scope->get(id.name, false)) != nullptr && ref->type == AST::Type::FunctionArgument) {
      ast = ref;
    } else {
      auto uid = uniqueString(id.name);

      ast = std::make_shared<AST::ID>(m_ast->strings[uid], uid);
      ast->loc = id.loc;

      if ((ref = m_scope->get(id.name)) && !m_scope->isInCurrentScope(id.name)) {
        ast->typeInfo = ref->typeInfo;
        if (ref->type == AST::Type::FunctionArgument) {
          AST::asFunctionArgument(ref)->isCaptured = true;
          m_scope->scopeFor(id.name)->isRequired = true;
          m_scope->capturesScope = true;
        }
      } else {
        ast->typeInfo = m_typeInfo[id.name];
      }
    }

    TypeMap newTypes;
    if (parseGenerics(newTypes)) {
      types = &newTypes;
    }

    while (true) {
      if (m_lexer.skip(Token::Type::COLON)) {
        ast->typeInfo = parseType(types);
      } else if (m_lexer.token()->type == Token::Type::L_PAREN) {
        ast = parseCall(std::move(ast), types);
      } else if (ast->type == AST::Type::Call &&  m_lexer.token()->type == Token::Type::L_BRACE) {
        auto call = AST::asCall(ast);
        ast = parseFunction(std::move(call), types);
      } else {
        break;
      }
    }

    if (ast->type == AST::Type::Call) {
      typeCheck(AST::asCall(ast));
    }

    return ast;
  }

  std::shared_ptr<AST::Function> Parser::parseFunction(std::shared_ptr<AST::Call> &&call, TypeMap *types) {
    assert(call->callee->type == AST::Type::ID);

    auto fn = std::make_shared<AST::Function>();
    fn->name = AST::asID(call->callee);
    if (InterfaceTypeInfo != nullptr) {
      if (!(fn->typeInfo = InterfaceTypeInfo->operator[](fn->name->name))) {
        fprintf(stderr, "Defining function `%s` that does not have type information\n", fn->name->name.c_str());
        throw std::runtime_error("Missing type infomation");
      }
    } else {
      fn->typeInfo = new TypeChain();
    }

    fn->name->name += ImplementationTypeName;
    fn->name->uid = uniqueString(fn->name->name);
    m_scope->set(fn->name->name, fn);
    m_scope->isRequired = true;

    m_scope = m_scope->create();

    unsigned i = 0;
    for (auto arg : call->arguments) {
      std::string argName;
      if (arg->type == AST::Type::ID) {
        argName = AST::asID(arg)->name;
      } else if (arg->type == AST::Type::FunctionArgument) {
        argName = AST::asFunctionArgument(arg)->name;
      } else {
        perror("Can't handle argument type on function declaration");
        throw;
      }

      auto fnArg = std::make_shared<AST::FunctionArgument>(argName, i++);

      if (!InterfaceTypeInfo) {
        if (!arg->typeInfo) {
          fprintf(stderr, "Defining function `%s` that does not have type information\n", fn->name->name.c_str());
          throw std::runtime_error("Missing type infomation");
        }
        fn->getTypeInfo()->types.push_back((fnArg->typeInfo = arg->typeInfo));
      } else {
        fnArg->typeInfo = fn->getTypeInfo()->types[i - 1];
      }


      GenericType *gt;
      if ((gt = dynamic_cast<GenericType *>(fnArg->typeInfo))) {
        if (!types->operator[](gt->typeName) && !(fnArg->typeInfo = ImplementationTypes->operator[](gt->typeName))) {
          throw std::runtime_error("Unknown type");
        }
        if (!InterfaceTypeInfo) {
          fn->getTypeInfo()->generics[gt->typeName] = GenericInitialValue;
        }
      }
      fn->arguments.push_back(fnArg);
      m_scope->set(argName, fnArg);
    }

    if (!InterfaceTypeInfo) {
      fn->getTypeInfo()->types.push_back(call->typeInfo);
      m_typeInfo[fn->name->name] = fn->getTypeInfo();
    }

    m_lexer.ensure(Token::Type::L_BRACE);
    fn->body = parseBlock(Token::Type::R_BRACE);
    fn->loc.start = fn->name->loc.start;
    fn->loc.end = m_lexer.token(Token::Type::R_BRACE)->loc.end;

    m_scope = m_scope->restore();

    return fn;
  }

  std::shared_ptr<AST::String> Parser::parseString() {
    auto string = static_cast<Token::String *>(m_lexer.token(Token::Type::STRING));

    auto uid = uniqueString(string->value);
    auto ast =  std::make_shared<AST::String>(m_ast->strings[uid], uid);
    ast->loc = string->loc;
    ast->typeInfo = m_types["String"];
    return ast;
  }

  std::shared_ptr<AST::Call> Parser::parseCall(std::shared_ptr<AST> &&callee, TypeMap *types) {
    auto start = callee->loc.start;

    m_lexer.ensure(Token::Type::L_PAREN);

    auto call = std::make_shared<AST::Call>();
    call->callee = callee;

    while (m_lexer.token()->type != Token::Type::R_PAREN) {
      auto argument = parseFactor(types);
      call->arguments.push_back(argument);
      if (m_lexer.token()->type != Token::Type::R_PAREN) {
        m_lexer.ensure(Token::Type::COMMA);
      }
    }

    auto end = m_lexer.token(Token::Type::R_PAREN)->loc.end;
    call->loc = { start, end };

    TypeChain *typeInfo;
    if ((typeInfo = dynamic_cast<TypeChain *>(callee->typeInfo))) {
      call->typeInfo = typeInfo->returnType();
    }

    return call;
  }

  Type *Parser::parseType(TypeMap *types) {
    if (m_lexer.skip(Token::Type::L_PAREN)) {
      TypeChain *typeInfo = new TypeChain();
      while (!m_lexer.skip(Token::Type::R_PAREN)) {
        typeInfo->types.push_back(parseType(types));
        if (m_lexer.token()->type != Token::Type::R_PAREN) {
          m_lexer.ensure(Token::Type::COMMA);
        }
      }
      m_lexer.ensure(Token::Type::COLON);
      typeInfo->types.push_back(parseType(types));
      return typeInfo;
    } else {
      auto typeString = static_cast<Token::ID *>(m_lexer.token(Token::Type::ID))->name;
      auto type = types ? types->operator[](typeString) : nullptr;
      return type ?: m_types[typeString];
    }
  }

  std::shared_ptr<AST::Block> Parser::parseInterface() {
    auto interface = new TypeInterface();
    interface->name = ((Token::ID *)m_lexer.token(Token::Type::ID))->name;

    m_lexer.ensure(Token::Type::L_ANGLE);
    interface->genericName = static_cast<Token::ID *>(m_lexer.token(Token::Type::ID))->name;
    m_lexer.ensure(Token::Type::R_ANGLE);

    m_lexer.ensure(Token::Type::L_BRACE);

    auto block = std::make_shared<AST::Block>();
    while (!m_lexer.skip(Token::Type::R_BRACE)) {
      auto next = ((Token::ID *)m_lexer.token())->name;
      if (next != "virtual") {
        auto node = parseID();
        if (node) {
          block->nodes.push_back(node);
        }
      } else {
        m_lexer.ensure(Token::Type::ID);

        auto name = static_cast<Token::ID *>(m_lexer.token(Token::Type::ID))->name;
        auto typeInfo = parsePrototype();

        m_types[name] = interface;
        interface->functions[name] = typeInfo;
        m_typeInfo[name] = typeInfo;
      }
    }

    m_types[interface->name] = interface;

    return block;
  }

  std::shared_ptr<AST> Parser::parseImplementation() {
    auto implementation = new TypeImplementation();
    auto name = static_cast<Token::ID *>(m_lexer.token(Token::Type::ID))->name;
    auto interface = dynamic_cast<TypeInterface *>(m_types[name]);
    if (!interface) {
      throw std::runtime_error("Cannot find interface for implementation");
    }

    m_lexer.ensure(Token::Type::L_ANGLE);
    Token::ID *type = static_cast<Token::ID *>(m_lexer.token(Token::Type::ID));
    implementation->type = m_types[type->name];
    m_lexer.ensure(Token::Type::R_ANGLE);

    std::unordered_map<std::string, Type *> t;
    t[interface->genericName] = implementation->type;

    InterfaceTypeInfo = &interface->functions;
    ImplementationTypes = &t;
    ImplementationTypeName = implementation->type->toString();

    m_lexer.ensure(Token::Type::L_BRACE);
    auto block = parseBlock(Token::Type::R_BRACE);
    m_lexer.ensure(Token::Type::R_BRACE);

    InterfaceTypeInfo = nullptr;
    ImplementationTypes = nullptr;
    ImplementationTypeName = "";

    implementation->interface = interface;
    interface->implementations[implementation->type] = implementation;

    block->needsScope = false;
    return block;
  }
  
  TypeChain *Parser::parsePrototype() {
    auto typeInfo = new TypeChain();

    parseGenerics(typeInfo->generics);

    m_lexer.ensure(Token::Type::L_PAREN);
    while (m_lexer.token()->type != Token::Type::R_PAREN) {
      typeInfo->types.push_back(parseType(&typeInfo->generics));
      if (!m_lexer.skip(Token::Type::COMMA)) {
        break;
      }
    }
    m_lexer.ensure(Token::Type::R_PAREN);

    m_lexer.ensure(Token::Type::COLON);
    typeInfo->types.push_back(parseType(&typeInfo->generics));

    return typeInfo;
  }

  bool Parser::parseGenerics(TypeMap &generics) {
    if (m_lexer.skip(Token::Type::L_ANGLE)) {
      do {
        auto t = static_cast<Token::ID *>(m_lexer.token(Token::Type::ID));
        generics[t->name] = new GenericType(t->name);
        if (m_lexer.skip(Token::Type::R_ANGLE)) {
          break;
        }
      } while(m_lexer.skip(Token::Type::COMMA));

      return false;
    }

    return true;
  }

  void Parser::typeCheck(std::shared_ptr<AST::Call> &&call) {
    if (call->callee->type == AST::Type::ID) {
      auto callee = AST::asID(call->callee);
      auto &calleeName = callee->name;
      auto typeInfo = m_typeInfo[calleeName];
      if (!typeInfo) {
        fprintf(stderr, "Missing type information for `%s`\n", calleeName.c_str());
        throw;
      }

      if (call->arguments.size() != typeInfo->types.size() - 1) {
        fprintf(stderr, "Invalid type");
        throw;
      }

      TypeMap generics = typeInfo->generics;
      for (unsigned i = 0; i < typeInfo->types.size() - 1; i++) {
        Type* expected = typeInfo->types[i];
        Type* actual = call->arguments[i]->typeInfo;

        GenericType *gt;
        if ((gt = dynamic_cast<GenericType *>(expected))) {
          Type *t;
          if ((t = generics[gt->typeName])) {
            if (t == GenericInitialValue) {
              generics[gt->typeName] = actual;
              continue;
            } else {
              expected = t;
            }
          } else {
            auto impl = dynamic_cast<TypeInterface *>(m_types[calleeName])->implementations[actual];
            if (!impl) {
              fprintf(stderr, "Couldn't find implementation for `%s` with signature `%s`", calleeName.c_str(), call->typeInfo->toString().c_str());
              throw;
            }

            callee->name += impl->type->toString();
            callee->uid = uniqueString(callee->name);
            expected = impl->type;
          }
        }

        TypeInterface *interface;
        if ((interface = dynamic_cast<TypeInterface *>(expected))) {
          if (interface->implementations[actual] != nullptr) {
            continue;
          }
        }

        if (!actual->equals(expected)) {
          fprintf(stderr, "Expected `%s` but got `%s`\n", expected->toString().c_str(), actual->toString().c_str());
          throw;
        }
      }

      GenericType *gt;
      if ((gt = dynamic_cast<GenericType *>(call->typeInfo))) {
        Type *t;
        if ((t = generics[gt->typeName])) {
          if (t == GenericInitialValue) {
            throw std::runtime_error("Cannot resolve generic return type for function");
          } else {
            call->typeInfo = t;
          }
        }
      }

    }
  }

  // Helpers
  //
  unsigned Parser::uniqueString(std::string &str) {
    unsigned uid;
    auto it = std::find(m_ast->strings.begin(), m_ast->strings.end(), str);
    if (it != m_ast->strings.end()) {
      uid = it - m_ast->strings.begin();
    } else {
      uid = str_uid++;
      m_ast->strings.push_back(str);
    }
    return uid;
  }

}