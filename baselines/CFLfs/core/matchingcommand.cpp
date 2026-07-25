



#include "matchingcommand.h"

namespace {

constexpr const char* kDefaultDataGraphDir = "data/graph";
constexpr const char* kDefaultQueryGraphDir = "data/queries";
}

MatchingCommand::MatchingCommand(const int argc, char **argv) : CommandParser(argc, argv) {
    
    options_key[OptionKeyword::Algorithm] = "-a";
    options_key[OptionKeyword::IndexType] = "-i";
    options_key[OptionKeyword::QueryGraphFile] = "-q";
    options_key[OptionKeyword::DataGraphFile] = "-d";
    options_key[OptionKeyword::ThreadCount] = "-n";
    options_key[OptionKeyword::DepthThreshold] = "-d0";
    options_key[OptionKeyword::WidthThreshold] = "-w0";
    options_key[OptionKeyword::Filter] = "-filter";
    options_key[OptionKeyword::Order] = "-order";
    options_key[OptionKeyword::Engine] = "-engine";
    options_key[OptionKeyword::MaxOutputEmbeddingNum] = "-num";
    options_key[OptionKeyword::SpectrumAnalysisTimeLimit] = "-time_limit";
    options_key[OptionKeyword::SpectrumAnalysisOrderNum] = "-order_num";
    options_key[OptionKeyword::DistributionFilePath] = "-dis_file";
    options_key[OptionKeyword::CSRFilePath] = "-csr";
    options_key[OptionKeyword::TopK] = "-topk";
    
    options_key[OptionKeyword::BatchQueryDir] = "-batch_dir";
    options_key[OptionKeyword::VertexLabelMatch] = "-vlmatch";
    processOptions();
};

void MatchingCommand::processOptions() {
    
    options_value[OptionKeyword::QueryGraphFile] = getCommandOption(options_key[OptionKeyword::QueryGraphFile]);;

    
    options_value[OptionKeyword::DataGraphFile] = getCommandOption(options_key[OptionKeyword::DataGraphFile]);

    
    options_value[OptionKeyword::Algorithm] = getCommandOption(options_key[OptionKeyword::Algorithm]);

    
    options_value[OptionKeyword::ThreadCount] = getCommandOption(options_key[OptionKeyword::ThreadCount]);

    
    options_value[OptionKeyword::DepthThreshold] = getCommandOption(options_key[OptionKeyword::DepthThreshold]);

    
    options_value[OptionKeyword::WidthThreshold] = getCommandOption(options_key[OptionKeyword::WidthThreshold]);

    
    options_value[OptionKeyword::IndexType] = getCommandOption(options_key[OptionKeyword::IndexType]);

    
    options_value[OptionKeyword::Filter] = getCommandOption(options_key[OptionKeyword::Filter]);

    
    options_value[OptionKeyword::Order] = getCommandOption(options_key[OptionKeyword::Order]);

    
    options_value[OptionKeyword::Engine] = getCommandOption(options_key[OptionKeyword::Engine]);

    
    options_value[OptionKeyword::MaxOutputEmbeddingNum] = getCommandOption(options_key[OptionKeyword::MaxOutputEmbeddingNum]);

    
    options_value[OptionKeyword::SpectrumAnalysisTimeLimit] = getCommandOption(options_key[OptionKeyword::SpectrumAnalysisTimeLimit]);

    
    options_value[OptionKeyword::SpectrumAnalysisOrderNum] = getCommandOption(options_key[OptionKeyword::SpectrumAnalysisOrderNum]);

    
    options_value[OptionKeyword::DistributionFilePath] = getCommandOption(options_key[OptionKeyword::DistributionFilePath]);

    
    options_value[OptionKeyword::CSRFilePath] = getCommandOption(options_key[OptionKeyword::CSRFilePath]);

    
    options_value[OptionKeyword::TopK] = getCommandOption(options_key[OptionKeyword::TopK]);

    options_value[OptionKeyword::BatchQueryDir] = getCommandOption(options_key[OptionKeyword::BatchQueryDir]);

    options_value[OptionKeyword::VertexLabelMatch] = getCommandOption(options_key[OptionKeyword::VertexLabelMatch]);

    if (options_value[OptionKeyword::DataGraphFile].empty()) {
        options_value[OptionKeyword::DataGraphFile] = kDefaultDataGraphDir;
    }
    if (options_value[OptionKeyword::QueryGraphFile].empty()) {
        options_value[OptionKeyword::QueryGraphFile] = kDefaultQueryGraphDir;
    }
}
