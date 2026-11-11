#include "node_endpoints.h"

#include <cstring>
#include <map>
#include <set>
#include <vector>

#include <esp_log.h>
#include <esp_matter_endpoint.h>

#include <app/clusters/mode-select-server/SupportedModesManager.h>

static const char *TAG = "reair.ep";

using namespace esp_matter;
using namespace chip::app::Clusters;

namespace reair {
namespace {

struct ModeDef {
    uint8_t mode;
    const char *label;
};

const ModeDef period_modes[] = {{period_mode::s60, "60 s"}, {period_mode::s90, "90 s"}, {period_mode::s120, "120 s"}};
const ModeDef role_modes[] = {{role::master, "Leader"}, {role::slave, "Follower"}, {role::independent, "Independent"}};
const ModeDef direction_mode_modes[] = {{direction_mode::reciprocating, "Alternating"},
                                        {direction_mode::fixed_forward, "Forward"},
                                        {direction_mode::fixed_reverse, "Reverse"}};
const ModeDef forward_modes[] = {{forward_direction::intake, "Intake"}, {forward_direction::exhaust, "Exhaust"}};

// 所有 ModeSelect 端点共用一个 SupportedModesManager,按端点号返回各自的选项
class ModesManager : public ModeSelect::SupportedModesManager {
public:
    using Option = ModeSelect::Structs::ModeOptionStruct::Type;

    void add(uint16_t endpoint, const ModeDef *defs, size_t count)
    {
        std::vector<Option> &options = options_[endpoint];
        options.clear();
        options.reserve(count);
        for (size_t i = 0; i < count; i++) {
            Option opt;
            opt.label = chip::CharSpan::fromCharString(defs[i].label);
            opt.mode = defs[i].mode;
            opt.semanticTags = chip::app::DataModel::List<const ModeSelect::Structs::SemanticTagStruct::Type>();
            options.push_back(opt);
        }
    }

    ModeOptionsProvider getModeOptionsProvider(chip::EndpointId endpoint) const override
    {
        auto it = options_.find(endpoint);
        if (it == options_.end()) {
            return ModeOptionsProvider();
        }
        const Option *begin = it->second.data();
        return ModeOptionsProvider(begin, begin + it->second.size());
    }

    void set_write_lock(bool (*is_locked)(uint16_t)) { is_locked_ = is_locked; }
    void set_read_only(uint16_t endpoint) { read_only_.insert(endpoint); }
    // 只接受模式 0(清除),其他取值由本节点自己设置
    void set_clear_only(uint16_t endpoint) { clear_only_.insert(endpoint); }

    // ChangeToMode 先调用这个函数,它的返回值直接成为命令的结果,所以写入限制放在这里
    chip::Protocols::InteractionModel::Status getModeOptionByMode(chip::EndpointId endpoint, uint8_t mode,
                                                                  const Option **dataPtr) const override
    {
        if (read_only_.count(endpoint) != 0 || (clear_only_.count(endpoint) != 0 && mode != 0) || (is_locked_ != nullptr && is_locked_(endpoint))) {
            return chip::Protocols::InteractionModel::Status::InvalidInState;
        }
        auto it = options_.find(endpoint);
        if (it != options_.end()) {
            for (const Option &opt : it->second) {
                if (opt.mode == mode) {
                    *dataPtr = &opt;
                    return chip::Protocols::InteractionModel::Status::Success;
                }
            }
        }
        return chip::Protocols::InteractionModel::Status::ConstraintError;
    }

private:
    std::map<uint16_t, std::vector<Option>> options_;
    std::set<uint16_t> read_only_;
    std::set<uint16_t> clear_only_;
    bool (*is_locked_)(uint16_t) = nullptr;
};

ModesManager modes_manager;

endpoint_t *create_mode_select(node_t *node, const char *description, const ModeDef *defs, size_t count,
                               uint8_t initial, uint16_t &endpoint_id)
{
    endpoint::mode_select::config_t config;
    strlcpy(config.mode_select.description, description, sizeof(config.mode_select.description));
    config.mode_select.current_mode = initial;
    config.mode_select.delegate = &modes_manager;
    endpoint_t *ep = endpoint::mode_select::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
    if (ep == nullptr) {
        ESP_LOGE(TAG, "Failed to create ModeSelect endpoint %s", description);
        return nullptr;
    }
    endpoint_id = endpoint::get_id(ep);
    modes_manager.add(endpoint_id, defs, count);
    ESP_LOGI(TAG, "%s: endpoint %u", description, endpoint_id);
    return ep;
}

const ModeDef direction_states[] = {{actual_direction_state::intake, "Intake"},
                                    {actual_direction_state::exhaust, "Exhaust"},
                                    {actual_direction_state::stopped, "Stopped"}};
const ModeDef warning_states[] = {{warning_state::none, "None"}, {warning_state::warning, "Warning"}};

// 状态端点的 CurrentMode 只由本节点更新,控制器的 ChangeToMode 一律拒绝
void update_state(uint16_t endpoint_id, uint8_t state)
{
    esp_matter_attr_val_t val = esp_matter_uint8(state);
    esp_err_t err = attribute::report(endpoint_id, ModeSelect::Id, ModeSelect::Attributes::CurrentMode::Id, &val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "update state of endpoint %u failed: %d", endpoint_id, err);
    }
}

uint16_t actual_direction_endpoint = 0;
uint16_t warning_endpoint = 0;

} // namespace

esp_err_t create_endpoints(node_t *node, Endpoints &eps)
{
    bool ok = create_mode_select(node, "Cycle", period_modes, 3, period_mode::s60, eps.period) &&
        create_mode_select(node, "Role", role_modes, 3, role::independent, eps.role) &&
        create_mode_select(node, "Forward Direction", forward_modes, 2, forward_direction::exhaust, eps.forward_direction) &&
        create_mode_select(node, "Actual Direction", direction_states, 3, actual_direction_state::stopped,
                           eps.actual_direction) &&
        create_mode_select(node, "Warning", warning_states, 2, warning_state::none, eps.warning) &&
        create_mode_select(node, "Direction Mode", direction_mode_modes, 3, direction_mode::reciprocating,
                           eps.direction_mode);
    actual_direction_endpoint = eps.actual_direction;
    warning_endpoint = eps.warning;
    modes_manager.set_read_only(eps.actual_direction);
    modes_manager.set_clear_only(eps.warning);
    return ok ? ESP_OK : ESP_FAIL;
}

void set_write_lock(bool (*is_locked)(uint16_t endpoint_id))
{
    modes_manager.set_write_lock(is_locked);
}

void set_actual_direction(uint8_t state)
{
    update_state(actual_direction_endpoint, state);
}

void set_warning(uint8_t state)
{
    update_state(warning_endpoint, state);
}

} // namespace reair
