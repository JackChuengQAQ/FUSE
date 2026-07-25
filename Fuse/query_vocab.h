#ifndef FUSE_QUERY_VOCAB_H
#define FUSE_QUERY_VOCAB_H

#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "configuration/types.h"
#include "new_graph.h"





struct QueryVocabMap {
    
    std::vector<ui> rel_q2k;
    
    std::vector<LabelID> label_q2k;
    
    
    
    std::vector<std::unordered_set<LabelID> > node_kg_labels;
};


inline void buildKgNameTables(const EmbeddingDiGraph& kg,
                              std::unordered_map<std::string, ui>& kg_rel_name_to_id,
                              std::unordered_map<std::string, LabelID>& kg_label_name_to_id) {
    kg_rel_name_to_id.clear();
    kg_label_name_to_id.clear();
    kg_rel_name_to_id.reserve(static_cast<size_t>(kg.getRelationsCount()) * 2 + 16);
    kg_label_name_to_id.reserve(static_cast<size_t>(kg.getLabelsCount()) * 2 + 16);
    for (ui r = 0; r < kg.getRelationsCount(); ++r) {
        kg_rel_name_to_id[kg.getRelationName(r)] = r;
    }
    for (ui l = 0; l < kg.getLabelsCount(); ++l) {
        kg_label_name_to_id[kg.getTypeName(l)] = static_cast<LabelID>(l);
    }
}








inline QueryVocabMap buildQueryVocabMap(
    const EmbeddingDiGraph& query_graph,
    const std::vector<VertexID>& old_to_new,
    ui new_count,
    const std::unordered_map<std::string, ui>& kg_rel_name_to_id,
    const std::unordered_map<std::string, LabelID>& kg_label_name_to_id) {
    QueryVocabMap m;
    const ui kInvalid = std::numeric_limits<ui>::max();

    m.rel_q2k.assign(query_graph.getRelationsCount(), kInvalid);
    for (ui r = 0; r < query_graph.getRelationsCount(); ++r) {
        std::unordered_map<std::string, ui>::const_iterator it =
            kg_rel_name_to_id.find(query_graph.getRelationName(r));
        if (it != kg_rel_name_to_id.end()) {
            m.rel_q2k[r] = it->second;
        }
    }

    m.label_q2k.assign(query_graph.getLabelsCount(), static_cast<LabelID>(kInvalid));
    for (ui l = 0; l < query_graph.getLabelsCount(); ++l) {
        std::unordered_map<std::string, LabelID>::const_iterator it =
            kg_label_name_to_id.find(query_graph.getTypeName(l));
        if (it != kg_label_name_to_id.end()) {
            m.label_q2k[l] = it->second;
        }
    }

    m.node_kg_labels.assign(new_count, std::unordered_set<LabelID>());
    for (VertexID old_u = 0; old_u < query_graph.getVerticesCount(); ++old_u) {
        if (old_u >= old_to_new.size()) {
            continue;
        }
        VertexID new_u = old_to_new[old_u];
        if (new_u >= new_count) {
            continue;
        }
        ui cnt = 0;
        const LabelID* lbls = query_graph.getVertexLabels(old_u, cnt);
        for (ui i = 0; i < cnt; ++i) {
            LabelID q_lid = lbls[i];
            if (q_lid >= m.label_q2k.size()) {
                continue;
            }
            LabelID kg_lid = m.label_q2k[q_lid];
            if (kg_lid == static_cast<LabelID>(kInvalid)) {
                continue;
            }
            m.node_kg_labels[new_u].insert(kg_lid);
        }
    }

    return m;
}

#endif
