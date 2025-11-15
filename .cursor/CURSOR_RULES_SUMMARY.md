# Cursor Rules Generated for Ladybird

This document summarizes the Cursor Rules that have been generated for the Ladybird browser project.

## Location

All Cursor Rules are located in: `.cursor/rules/`

## Generated Rule Files

### 1. **coding-style.mdc** (Always Applied)
Comprehensive C++ coding style rules covering:
- Naming conventions (snake_case, CamelCase, SCREAMING_CASE)
- Data member prefixes (m_, s_, g_)
- Getter/setter patterns
- Type conventions (unsigned, const placement)
- Cast usage (no C-style casts)
- Class vs struct guidelines
- Header guards (#pragma once)
- Constructor initialization
- Control structures and loops
- Virtual method overrides
- Using statements
- Enum conventions

**Source:** Documentation/CodingStyle.md

### 2. **error-handling.mdc** (Always Applied)
Error handling patterns and macros:
- TRY(...) macro for error propagation with ErrorOr<T>
- MUST(...) macro for operations that must succeed
- Fallible constructor pattern (static create() methods)
- LibWeb error types:
  - AK::ErrorOr<T> for OOM errors
  - WebIDL::ExceptionOr<T> for web APIs
  - WebIDL::SimpleException for JS errors
  - WebIDL::DOMException for web-specific errors
  - JS::ThrowCompletionOr<T> for LibJS completions

**Source:** Documentation/Patterns.md, Documentation/LibWebPatterns.md

### 3. **smart-pointers.mdc** (Always Applied)
Smart pointer usage and ownership semantics:
- OwnPtr<T>/NonnullOwnPtr<T> for single ownership
- RefPtr<T>/NonnullRefPtr<T> for shared ownership
- WeakPtr<T> for observation without ownership
- Helper functions: make<T>(), try_make<T>(), make_ref_counted<T>()
- Manual construction with adopt_own(), adopt_ref()
- When to use each pointer type
- Common patterns and best practices

**Source:** Documentation/SmartPointers.md

### 4. **cpp-patterns.mdc** (Applied to *.cpp, *.h, *.mm)
General C++ patterns used throughout the codebase:
- ladybird_main() entry point pattern
- Collection types: Array, Vector, FixedArray, Span
- String view literals (operator""sv)
- Source location for debugging (AK::SourceLocation)
- Static assertions with debug info (AssertSize)
- Intrusive lists for OOM-safety
- Comment conventions (FIXME, TODO)
- Type safety preferences (const over #define, inline over macros)

**Source:** Documentation/Patterns.md

### 5. **libweb-patterns.mdc** (Applied to Libraries/LibWeb/**/*)
LibWeb-specific development patterns:
- Directory structure (one namespace per spec)
- Spec implementation comment requirements:
  - Spec link above function
  - Step-by-step comments matching spec
  - FIXME for unimplemented steps
  - OPTIMIZATION comments for fast paths
- IDL file conventions (copy verbatim, 4-space indent)
- File placement (.cpp, .h, .idl together)
- C++ naming for web interfaces
- LibWeb error handling hierarchy
- Spec algorithm templates

**Source:** Documentation/LibWebPatterns.md

### 6. **contribution-guidelines.mdc** (Always Applied)
Contribution and commit message standards:
- Commit message format: "Category: Description"
- Category selection (LibWeb, LibJS, AK, etc.)
- Git workflow (atomic commits, rebase on master)
- Code quality standards
- Testing requirements
- Review process
- Language and style guidelines (American English, technical tone)
- AI/LLM usage policy
- Project values and governance

**Source:** CONTRIBUTING.md

### 7. **project-overview.mdc** (Always Applied)
High-level project structure and architecture:
- Multi-process architecture description
- Core library purposes and responsibilities
- Directory structure overview
- Documentation index with links
- Build system information
- Community resources
- License information

**Source:** README.md, Documentation/ProcessArchitecture.md

### 8. **README.mdc** (Manual - Description-based)
Quick reference guide for all Cursor Rules:
- Summary of each rule file
- Quick reference code snippets
- Usage patterns for common scenarios
- Navigation guide to detailed documentation

## How Rules Are Applied

### Automatic Application
These rules are **always applied** to every request:
- coding-style.mdc
- error-handling.mdc
- smart-pointers.mdc
- contribution-guidelines.mdc
- project-overview.mdc

### Pattern-Based Application
These rules apply automatically when working with matching files:
- cpp-patterns.mdc → `*.cpp`, `*.h`, `*.mm` files
- libweb-patterns.mdc → `Libraries/LibWeb/**/*` files

### Manual Application
- README.mdc → Can be manually invoked via description search

## Benefits

These Cursor Rules provide:

1. **Consistent Style**: Automatic adherence to Ladybird coding standards
2. **Error Prevention**: Guidance on proper error handling patterns
3. **Memory Safety**: Smart pointer usage patterns to prevent leaks
4. **Web Standards**: Spec implementation patterns for LibWeb
5. **Contribution Quality**: Proper commit message and PR formatting
6. **Project Knowledge**: Quick access to architecture and library purposes
7. **Context-Aware**: Different patterns for different parts of the codebase

## Maintenance

These rules are based on the official Ladybird documentation as of October 2025. If the project's coding standards evolve, the rule files should be updated to reflect those changes.

To update rules:
1. Edit the appropriate `.mdc` file in `.cursor/rules/`
2. Follow the frontmatter format (alwaysApply, globs, or description)
3. Use `[filename](mdc:path/to/file)` to reference other files

## Quick Start Examples

### Writing a New Class
```cpp
// Follows coding-style.mdc
class MyParser {
public:
    static ErrorOr<NonnullOwnPtr<MyParser>> create();  // Fallible constructor
    
    void set_input(String input);  // Setter with 'set_' prefix
    String const& input() const { return m_input; }  // Getter, east const
    
private:
    MyParser() = default;
    String m_input;  // Member with m_ prefix
};
```

### Error Handling
```cpp
// Follows error-handling.mdc
ErrorOr<String> process_file(StringView path)
{
    auto file = TRY(Core::File::open(path, Core::File::OpenMode::Read));
    auto contents = TRY(file->read_until_eof());
    return String::from_utf8(contents);
}
```

### Smart Pointers
```cpp
// Follows smart-pointers.mdc
class Document : public RefCounted<Document> {
public:
    static ErrorOr<NonnullRefPtr<Document>> create()
    {
        return try_make_ref_counted<Document>();
    }
};
```

### LibWeb Spec Implementation
```cpp
// Follows libweb-patterns.mdc
// https://html.spec.whatwg.org/#dom-document-title
String Document::title()
{
    // 1. Let element be the first title element in the document in tree order, if there is one,
    //    or null otherwise.
    auto* element = first_title_element();
    
    // 2. If element is null, then return the empty string.
    if (!element)
        return String {};
    
    // 3. Return element's text content.
    return element->text_content();
}
```

### Commit Messages
```
LibWeb: Implement Document.title getter

LibJS+LibWeb: Add support for async generators

RequestServer: Fix memory leak in connection pool

AK: Optimize StringView comparison performance
```

## Resources

- **Full Documentation**: `Documentation/` directory
- **Coding Style**: `Documentation/CodingStyle.md`
- **Patterns**: `Documentation/Patterns.md`
- **LibWeb Patterns**: `Documentation/LibWebPatterns.md`
- **Smart Pointers**: `Documentation/SmartPointers.md`
- **Contributing**: `CONTRIBUTING.md`
- **Discord**: https://discord.gg/nvfjVJ4Svh

