#pragma once
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <vector>

typedef std::vector<std::unique_ptr<struct cmd_t>> cmd_list_t;
typedef std::map<std::string, uint64_t> cmd_idents_t;
typedef void* cmd_baton_t;

/* common utility functions for the command parser */
struct cmd_util_t {
    // more robust string to uint64
    static bool strtoll(const char* in, uint64_t& out, bool& neg);

    // levenshtein string distance function
    static uint32_t levenshtein(const char* s1, const char* s2);

    // substring match (return match length, or -1 if different)
    static int32_t str_match(const char* str, const char* sub);
};

/// Command output interface base class
/// this class brokers all output text writing from cmd_t classes during command execution.
/// using a specific class we can ensure consisten output and eliminate output race conditions.
/// a versatile iterface also makes it easy to output text in a range of formats.
/// cmd_output_t is also a factory for derived classes with specialisations.
///
struct cmd_output_t {
    uint32_t indent_;

    /// Create a cmd_output_t instance that will write directly to a file descriptor.
    ///
    /// - FILE* fd, the file descriptior that all output will be written to
    static cmd_output_t* create_output_stdio(FILE* fd);

    /* auto indent control class */
    struct indent_t {

        indent_t(uint32_t* ptr, uint32_t indent)
            : ptr_(ptr)
            , restore_(*ptr)
        {
            assert(ptr);
            *ptr += indent;
        }

        ~indent_t()
        {
            assert(ptr_);
            *ptr_ = restore_;
        }

        void add(uint32_t num)
        {
            assert(ptr_);
            *ptr_ += num;
        }

    protected:
        uint32_t* ptr_;
        uint32_t restore_;
    };

    /* output mutex guard */
    struct guard_t {
        cmd_output_t& out_;
        guard_t(cmd_output_t& out)
            : out_(out)
        {
            out.lock();
        }

        ~guard_t()
        {
            out_.unlock();
        }
    };

    guard_t guard()
    {
        return guard_t(*this);
    }

    cmd_output_t()
        : indent_(2)
    {
    }

    virtual ~cmd_output_t() {}

    virtual void lock() = 0;
    virtual void unlock() = 0;

    indent_t indent_push(uint32_t next)
    {
        return indent_t(&indent_, next);
    }

    virtual void indent() = 0;

    virtual void print(bool indent, const char* fmt, ...) = 0;

    virtual void println(bool indent, const char* fmt, ...) = 0;
    virtual void println(bool indent, const char* fmt, va_list& args) = 0;

    virtual void eol() = 0;
};

/* command locale text definitions */
struct cmd_locale_t {
    static void possible_completions(cmd_output_t& out)
    {
        out.println(true, "possible completions:");
    }

    static void invalid_command(cmd_output_t& out)
    {
        out.println(true, "invalid command");
    }

    static void no_subcommand(cmd_output_t& out, const char* cmd)
    {
        out.println(true, "no subcommand '%s'", cmd);
    }

    static void did_you_meen(cmd_output_t& out)
    {
        out.println(true, "did you meen:");
    }

    static void not_val_or_ident(cmd_output_t& out)
    {
        out.println(true, "return type not value or identifier");
    }

    static void unknown_ident(cmd_output_t& out, const char* ident)
    {
        out.println(true, "unknown identifier '%s'", ident);
    }

    static void malformed_exp(cmd_output_t& out)
    {
        out.println(true, "malformed expression");
    }

    static void error(cmd_output_t& out, const char* err)
    {
        out.println(true, "error: %s", err);
    }

    static void usage(cmd_output_t& out, const char* path, const char* args, const char* desc)
    {
        out.println(true, "usage: %s %s", path, args ? args : "");
        if (desc) {
            out.println(true, "desc:  %s", desc);
        }
    }

    static void subcommands(cmd_output_t& out)
    {
        out.println(true, "subcomands:");
    }

    static void unable_to_find_cmd(cmd_output_t& out, const char* cmd)
    {
        out.println(true, "unable to find command '%s'", cmd);
    }

    static void num_aliases(cmd_output_t& out, uint64_t num)
    {
        num ? out.println(true, "%d aliases:", (uint32_t)num) : out.println(true, "no alises");
    }
};

/* command argument token */
struct cmd_token_t {
    cmd_token_t()
    {
    }

    cmd_token_t(const std::string& str)
        : token_(str)
    {
    }

    const std::string& get() const
    {
        return token_;
    }

    template <typename type_t>
    bool get(type_t& out) const
    {
        bool neg = false;
        uint64_t value = 0;
        if (!cmd_util_t::strtoll(token_.c_str(), value, neg)) {
            return false;
        }
        out = static_cast<type_t>(neg ? 0 - value : value);
        return true;
    }

    bool operator==(const cmd_token_t& rhs) const
    {
        return token_ == rhs.get();
    }

    template <typename type_t>
    bool operator==(const type_t& rhs) const
    {
        return token_ == rhs;
    }

    operator std::string() const
    {
        return token_;
    }

    const char* c_str() const
    {
        return token_.c_str();
    }

protected:
    std::string token_;
};

/* command arguments token list */
struct cmd_tokens_t {
    cmd_idents_t* idents_;

    cmd_tokens_t(cmd_idents_t* idents)
        : idents_(idents)
    {
    }

    size_t token_size() const
    {
        return tokens_.size();
    }

    bool get(std::string& out)
    {
        if (tokens_.empty()) {
            return false;
        }
        out = tokens_.front().get();
        tokens_.pop_front();
        return true;
    }

    bool get(uint64_t& out)
    {
        if (tokens_.empty()) {
            return false;
        }
        if (!tokens_.front().get(out)) {
            return false;
        }
        tokens_.pop_front();
        return true;
    }

    bool get(cmd_token_t& out)
    {
        if (tokens_.empty()) {
            return false;
        }
        out = tokens_.front();
        tokens_.pop_front();
        return true;
    }

    bool flag_get(const std::string& name) const
    {
        return !(flags_.find(name) == flags_.end());
    }

    bool pair_get(const std::string& name, cmd_token_t& out) const
    {
        auto itt = pairs_.find(name);
        if (itt == pairs_.end()) {
            return false;
        } else {
            return (out = itt->second), true;
        }
    }

    void push(std::string input)
    {
        /* flush when input is empty */
        if (input.empty()) {
            if (!stage_pair_.first.empty()) {
                flags_.insert(stage_pair_.first);
                stage_pair_.first.clear();
            }
            return;
        }
        /* process identifier substitution */
        if (idents_) {
            if (input[0] == '$') {
                const char* temp = input.c_str() + 1;
                auto itt = idents_->find(temp);
                if (itt != idents_->end()) {
                    const uint64_t val = itt->second;
                    // todo: convert to hex string
                    input = std::to_string(val);
                }
            }
        }
        /* add to raw token set */
        raw_.push_back(input);
        /* if we have a flag or switch */
        if (input.find("-") == 0) {
            if (!stage_pair_.first.empty()) {
                flags_.insert(stage_pair_.first);
            }
            stage_pair_.first = input;
        } else {
            if (!stage_pair_.first.empty()) {
                pairs_[stage_pair_.first] = input;
                stage_pair_.first.clear();
            } else {
                tokens_.push_back(input);
            }
        }
    }

    bool token_empty() const
    {
        return tokens_.empty();
    }

    const cmd_token_t& token_front() const
    {
        return tokens_.front();
    }

    bool token_pop()
    {
        if (!tokens_.empty()) {
            assert(!raw_.empty());
            if (raw_.front() == tokens_.front()) {
                // pop prefixed tokens
                raw_.pop_front();
                tokens_.pop_front();
                return true;
            }
        }
        assert(!"FIXME: failed to pop");
        return false;
    }

    const std::deque<cmd_token_t>& tokens() const
    {
        return tokens_;
    }

    const std::map<std::string, cmd_token_t>& pairs() const
    {
        return pairs_;
    }

    const std::set<std::string>& flags() const
    {
        return flags_;
    }

    const std::deque<cmd_token_t>& raw() const
    {
        return raw_;
    }

    const bool token_find(const std::string& in) const
    {
        for (const cmd_token_t& tok : tokens_) {
            if (tok == in) {
                return true;
            }
        }
        return false;
    }

protected:
    // raw tokens
    std::deque<cmd_token_t> raw_;
    // staging area for pairs
    std::pair<std::string, cmd_token_t> stage_pair_;
    // basic token arguments
    std::deque<cmd_token_t> tokens_;
    // key value pair arguments
    std::map<std::string, cmd_token_t> pairs_;
    // switch flag
    std::set<std::string> flags_;
};

/// cmd_t, the command base class
///
/// this is the base command class that should be extended to handle custom commands
/// a cmd_t can be a child of another command or inserted into a cmd_parser_t as a root command
/// two key methods can be overriden, on_execute and on_usage which the cmd_parser_t will invoke based on user input
///
struct cmd_t {
    // command name
    const char* const name_;
    // the owning command parser
    struct cmd_parser_t& parser_;
    // user data
    cmd_baton_t user_;
    // parent comand
    cmd_t* parent_;
    // sub command list
    cmd_list_t sub_;
    // command usage
    const char* usage_;
    const char* desc_;

    /// cmd_t constructor
    ///
    /// const char* name, the name of this command
    /// cmd_t* parent, the parent cmd_t instance
    /// cmd_baton_t user, the opaque user data passed to this cmd_t instance
    cmd_t(const char* name,
        cmd_parser_t& parser,
        cmd_t* parent,
        cmd_baton_t user = nullptr)
        : name_(name)
        , parser_(parser)
        , user_(user)
        , parent_(parent)
        , sub_()
        , usage_(nullptr)
        , desc_(nullptr)
    {
    }

    /// Add child command
    ///  instanciate and attach a new child command to this parent command
    ///  supplying this commands user_ data during its construction.
    ///
    /// - return, the new cmd_t instance
    template <typename type_t>
    type_t* add_sub_command()
    {
        return add_sub_command<type_t>(user_);
    }

    /// Add child command
    /// instanciate and attach a new child command to this parent command
    ///
    /// - cmd_baton_t user, opaque user data to pass to the new child cmd_t
    ///
    /// - return, the new cmd_t instance
    template <typename type_t>
    type_t* add_sub_command(cmd_baton_t user)
    {
        auto temp = std::unique_ptr<type_t>(new type_t(parser_, this, user));
        sub_.push_back(std::move(temp));
        return (type_t*)sub_.rbegin()->get();
    }

    /// Command execution handler
    ///
    /// - cmd_tokens_t& tok, token list of arguments supplied by the user
    /// - cmd_output_t& out, text output stream for writing results to
    ///
    /// - return, true if the command was interpreted successfully
    virtual bool on_execute(cmd_tokens_t& tok, cmd_output_t& out);

    /// Return string with hierarchy of parent commands
    ///
    /// - std::string& out, string to store output hierarchy
    void get_command_path(std::string& out) const
    {
        if (parent_) {
            parent_->get_command_path(out);
            out.append(" ");
        }
        out.append(name_);
    }

    /// Print command usage information to output stream
    ///
    /// - cmd_output_t& out, output stream to print to
    ///
    /// - return, true if usage was written successfully
    virtual bool on_usage(cmd_output_t& out) const
    {
        cmd_output_t::indent_t indent = out.indent_push(2);
        std::string path;
        get_command_path(path);
        cmd_locale_t::usage(out, path.c_str(), usage_, desc_);
        if (!sub_.empty()) {
            cmd_locale_t::subcommands(out);
            print_sub_commands(out);
        }
        return true;
    }

protected:
    /// Add an alias for this command
    ///
    /// - const std::string& name, the string name of the alias to add
    ///
    /// - return, true if the alias was associated successfully
    bool alias_add(const std::string& name);

    /// Report an error condition to the output stream
    ///
    /// - const char* fmt, format string
    /// - ..., var args
    ///
    /// - return, false so it can be propagated via return statements easily
    bool error(cmd_output_t& out, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        out.println(true, fmt, args);
        va_end(args);
        return false;
    }

    /// 
    void print_cmd_list(const cmd_list_t& list, cmd_output_t& out) const
    {
        cmd_output_t::indent_t indent = out.indent_push(2);
        for (const auto& cmd : list) {
            out.println(true, "%s", cmd->name_);
        }
    }

    void print_sub_commands(cmd_output_t& out) const
    {
        print_cmd_list(sub_, out);
    }
};

/// cmd_parser_t, the command parer
/// this type is the main workhorse of the command library.  it forms the root of the command hieararchy
/// it stores some state that can be accessed via all commands (alias_, idents_)
/// cmd_parser_t is responsible for parsing and dispatching user input to the appropriate command
/// 
struct cmd_parser_t {

    /// global user data passed to new subcommands unless overridden
    void* user_;

    /// root subcommand list
    cmd_list_t sub_;

    /// user input history
    std::vector<std::string> history_;

    /// map of alias names to command instances
    std::map<std::string, cmd_t*> alias_;

    /// expression identifier list
    cmd_idents_t idents_;

    /// cmd_parser_t constructor
    ///
    /// cmd_baton_t user, a global custom data pointer to be passed to any sub commands
    cmd_parser_t(cmd_baton_t user = nullptr)
        : user_(user)
    {
    }

    /// Get a string with the last user input to be executed.
    const std::string& last_cmd() const
    {
        return history_.back();
    }

    /// Add a new root command to the command parser
    ///
    /// the new subcommand instance will be passed the global cmd_parser_t user_ data
    template <typename type_t>
    type_t* add_command()
    {
        return add_command<type_t>(user_);
    }

    /// Add a new root command to the command parser
    ///
    /// - cmd_baton_t user, the user data type to pass to the sub command
    template <typename type_t>
    type_t* add_command(cmd_baton_t user)
    {
        cmd_t* parent = nullptr;
        std::unique_ptr<type_t> temp(new type_t(*this, parent, user));
        sub_.push_back(std::move(temp));
        return (type_t*)sub_.rbegin()->get();
    }

    /// Execute a command expression, calling the relevant cmd_t instance with
    /// 
    /// - const std::string& expr, string expression to execute
    /// - cmd_output_t* output, output stream used during execution
    bool execute(const std::string& expr, cmd_output_t* output);

    /// Add a new parser alias for a cmd_t instance.
    ///
    /// - cmd_t* cmd, command instance for which to make an alias
    /// - const std::string& alias, name for the alias
    bool alias_add(cmd_t* cmd, const std::string& alias);

    /// Remove a previously registered command alias byt name
    ///
    /// - const std::string& alias, the string command alias to remove
    bool alias_remove(const std::string& alias);

    /// Remove any previously registered command alises by target cmd_t
    ///
    /// - const cmd_t* cmd, the cmd_t instance to remove any alises to
    bool alias_remove(const cmd_t* cmd);

    /// Find a cmd_t instance given its alias name
    ///
    /// - const std::string& alias, the string alias to search for an associated cmd_t instance
    cmd_t* alias_find(const std::string& alias) const
    {
        auto itt = alias_.find(alias);
        return itt == alias_.end() ? nullptr : itt->second;
    }
};
