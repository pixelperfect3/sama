#include <iostream>

#include "tools/asset_tool/AssetProcessor.h"

using namespace engine::tools;

static void printUsage()
{
    std::cout << "Usage: sama-asset-tool [options]\n"
              << "\n"
              << "Options:\n"
              << "  --input <dir>       Input asset directory (required)\n"
              << "  --output <dir>      Output directory (required)\n"
              << "  --target <platform> Target platform: android, ios, desktop"
              << " (default: android)\n"
              << "  --tier <quality>    Quality tier: low, mid, high"
              << " (default: mid)\n"
              << "  --verbose           Print detailed progress\n"
              << "  --dry-run           Show what would be done without writing"
              << " files\n"
              << "  --help              Show this help\n";
}

int main(int argc, char* argv[])
{
    CliArgs args = parseArgs(argc, argv);

    if (args.help)
    {
        printUsage();
        return 0;
    }

    if (!args.valid)
    {
        std::cerr << "Error: " << args.errorMessage << "\n\n";
        printUsage();
        return 1;
    }

    AssetProcessor processor(args);
    return processor.run();
}
