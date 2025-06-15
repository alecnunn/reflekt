#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <memory>
#include <variant>
#include <optional>
#include <sstream>

using PropertyValue = std::variant<int, double, std::string, bool>;

class DynamicType;
class DynamicObject;

struct PropertyDescriptor {
    std::string name;
    std::string type_name;
    PropertyValue default_value;
    bool is_inherited = false;

    PropertyDescriptor(std::string  n, std::string  t, PropertyValue def = {}) : name(std::move(n)), type_name(std::move(t)), default_value(std::move(def)) {}
};

class TypeDescriptor {
public:
    std::string type_name;
    std::string base_type_name;
    std::vector<PropertyDescriptor> properties;

    explicit TypeDescriptor(std::string  name) : type_name(std::move(name)) {}

    void add_property(const std::string& name, const std::string& type, PropertyValue default_val = {}) {
        properties.emplace_back(name, type, std::move(default_val));
    }

    void set_base_type(const std::string& base) {
        base_type_name = base;
    }
};

class TypeRegistry {
private:
    std::unordered_map<std::string, std::unique_ptr<TypeDescriptor>> types_;
    std::unordered_map<std::string, std::vector<std::string>> inheritance_graph_;

public:
    static TypeRegistry& instance() {
        static TypeRegistry registry;
        return registry;
    }

    void register_type(std::unique_ptr<TypeDescriptor> type) {
        const auto& name = type->type_name;
        const auto& base = type->base_type_name;

        if (!base.empty()) {
            inheritance_graph_[name].push_back(base);
        }

        types_[name] = std::move(type);
    }

    [[nodiscard]] const TypeDescriptor* get_type(const std::string& name) const {
        const auto it = types_.find(name);
        return it != types_.end() ? it->second.get() : nullptr;
    }

    [[nodiscard]] std::vector<PropertyDescriptor> get_all_properties(const std::string& type_name) const {
        std::vector<PropertyDescriptor> all_props;
        collect_properties_recursive(type_name, all_props);
        return all_props;
    }

private:
    void collect_properties_recursive(const std::string& type_name, std::vector<PropertyDescriptor>& props) const {
        const auto type = get_type(type_name);
        if (!type) return;

        if (!type->base_type_name.empty()) {
            collect_properties_recursive(type->base_type_name, props);
        }

        for (const auto& prop : type->properties) {
            props.push_back(prop);
        }
    }
};

class DynamicObject {
private:
    std::string type_name_;
    std::unordered_map<std::string, PropertyValue> properties_;

public:
    explicit DynamicObject(std::string  type_name) : type_name_(std::move(type_name)) {
        const auto all_props = TypeRegistry::instance().get_all_properties(type_name_);
        for (const auto& prop : all_props) {
            properties_[prop.name] = prop.default_value;
        }
    }

    [[nodiscard]] const std::string& get_type_name() const { return type_name_; }

    template<typename T>
    void set_property(const std::string& name, const T& value) {
        properties_[name] = value;
    }

    template<typename T>
    [[nodiscard]] std::optional<T> get_property(const std::string& name) const {
        if (const auto it = properties_.find(name); it != properties_.end()) {
            if (std::holds_alternative<T>(it->second)) {
                return std::get<T>(it->second);
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] PropertyValue get_property_variant(const std::string& name) const {
        const auto it = properties_.find(name);
        return it != properties_.end() ? it->second : PropertyValue();
    }

    [[nodiscard]] std::vector<std::string> get_property_names() const {
        std::vector<std::string> names;
        for (const auto& [name, value] : properties_) {
            names.push_back(name);
        }

        return names;
    }

    [[nodiscard]] bool is_type(const std::string& type_name) const {
        if (type_name_ == type_name) return true;

        const auto all_props = TypeRegistry::instance().get_all_properties(type_name_);
        const auto target_props = TypeRegistry::instance().get_all_properties(type_name);

        for (const auto& target_prop : target_props) {
            bool found = false;
            for (const auto& our_prop : all_props) {
                if (our_prop.name == target_prop.name && our_prop.type_name == target_prop.type_name) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }

        return !target_props.empty();
    }
};

class ObjectFactory {
public:
    static std::unique_ptr<DynamicObject> create(const std::string& type_name) {
        if (auto type_desc = TypeRegistry::instance().get_type(type_name); !type_desc) {
            return nullptr;
        }

        return std::make_unique<DynamicObject>(type_name);
    }
};

class PropertyFileParser {
public:
    static std::unique_ptr<TypeDescriptor> parse_simple_format(const std::string& content) {
        auto lines = split_lines(content);
        if (lines.empty()) return nullptr;

        auto type_line = split(lines[0], ':');
        if (type_line.empty()) return nullptr;

        auto type_desc = std::make_unique<TypeDescriptor>(trim(type_line[0]));

        if (type_line.size() > 1) {
            type_desc->set_base_type(type_line[1]);
        }

        for (size_t i = 1; i < lines.size(); ++i) {
            if (auto prop_parts = split(lines[i], ':'); prop_parts.size() >= 2) {
                std::string prop_name = trim(prop_parts[0]);

                auto type_default = split(prop_parts[1], '=');
                std::string prop_type = trim(type_default[0]);

                PropertyValue default_val;
                if (type_default.size() > 1) {
                    std::string default_str = trim(type_default[1]);
                    default_val = parse_default_value(prop_type, default_str);
                }

                type_desc->add_property(prop_name, prop_type, default_val);
            }
        }

        return type_desc;
    }

private:
    static std::vector<std::string> split_lines(const std::string& str) {
        std::vector<std::string> lines;
        std::istringstream iss(str);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
        return lines;
    }

    static std::vector<std::string> split(const std::string& str, char delimiter) {
        std::vector<std::string> tokens;
        std::istringstream iss(str);
        std::string token;
        while (std::getline(iss, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }

    static std::string trim(const std::string& str) {
        const size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        const size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }

    static PropertyValue parse_default_value(const std::string& type, const std::string& value) {
        if (type == "int") {
            return std::stoi(value);
        } else if (type == "double") {
            return std::stod(value);
        } else if (type == "bool") {
            return value == "true" || value == "1";
        } else {
            return value;
        }
    }
};

void demonstrate_usage() {
    auto base_type = std::make_unique<TypeDescriptor>("Entity");
    base_type->add_property("id", "int", 0);
    base_type->add_property("name", "string", std::string(""));
    TypeRegistry::instance().register_type(std::move(base_type));

    auto derived_type = std::make_unique<TypeDescriptor>("Player");
    derived_type->set_base_type("Entity");
    derived_type->add_property("level", "int", 1);
    derived_type->add_property("health", "double", 100.0);
    TypeRegistry::instance().register_type(std::move(derived_type));

    // or parse from file
    const std::string property_content = R"(
Weapon: Entity
name: string = Test
damage: int = 5
range: double = 10.5
magical: bool
)";

    if (auto parsed_type = PropertyFileParser::parse_simple_format(property_content)) {
        TypeRegistry::instance().register_type(std::move(parsed_type));
    }

    if (const auto player = ObjectFactory::create("Player")) {
        player->set_property("name", std::string("Hero"));
        player->set_property("level", 42);
        player->set_property("health", 95.5);

        std::cout << "Player name: " << player->get_property<std::string>("name").value_or("Unknown") << "\n";
        std::cout << "Player level: " << player->get_property<int>("level").value_or(0) << "\n";
        std::cout << "Player health: " << player->get_property<double>("health").value_or(0.0) << "\n";

        std::cout << "Is Player an Entity? " << (player->is_type("Entity") ? "Yes" : "No") << "\n";
    }

    if (const auto weapon = ObjectFactory::create("Weapon")) {
        std::cout << "Weapon damage: " << weapon->get_property<int>("damage").value_or(0) << "\n";
        std::cout << "Weapon name: " << weapon->get_property<std::string>("name").value_or("Unknown") << "\n";
        std::cout << "Magical weapon: " << weapon->get_property<bool>("magical").value_or(false) << "\n";
        weapon->set_property("name", "Hello world");
        std::cout << "Weapon name: " << weapon->get_property<std::string>("name").value_or("Unknown") << "\n";
    }
}

int main() {
    demonstrate_usage();
    return 0;
}