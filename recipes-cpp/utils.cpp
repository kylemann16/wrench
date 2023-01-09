
#include "utils.hpp"

#include <iostream>
#include <chrono>

#include <pdal/PipelineManager.hpp>
#include <pdal/Stage.hpp>
#include <pdal/StageFactory.hpp>
#include <pdal/util/ThreadPool.hpp>

#include <gdal/gdal_utils.h>

using namespace pdal;

#include "progressbar.hpp"


std::mutex bar_mutex;
static progressbar* global_bar = nullptr;

static void bar_update()
{
    bar_mutex.lock();
    global_bar->update();
    bar_mutex.unlock();
}


// Table subclass that also takes care of updating progress in streaming pipelines
class MyTable : public FixedPointTable
{
public:
    MyTable(point_count_t capacity) : FixedPointTable(capacity) {}

protected:
    virtual void reset()
    {
        bar_update();
        FixedPointTable::reset();
    }
};


std::string box_to_pdal_bounds(const BOX2D &box)
{
    std::ostringstream oss;
    oss << std::fixed << "([" << box.minx << "," << box.maxx << "],[" << box.miny << "," << box.maxy << "])";
    return oss.str();  // "([xmin, xmax], [ymin, ymax])"
}


QuickInfo getQuickInfo(std::string inputFile)
{
    // TODO: handle the case when driver is not inferred
    std::string driver = StageFactory::inferReaderDriver(inputFile);
    if (driver.empty())
    {
        std::cout << "cannot infer driver for: " << inputFile << std::endl;
        return QuickInfo();
    }

    StageFactory factory;
    Stage *reader = factory.createStage(driver);  // reader is owned by the factory
    pdal::Options opts;
    opts.add("filename", inputFile);
    reader->setOptions(opts);
    //std::cout << "name " << reader->getName() << std::endl;
    return reader->preview();

    // PipelineManager m;
    // Stage &r = m.makeReader(inputFile, "");
    // return r.preview().m_pointCount;
}

MetadataNode getReaderMetadata(std::string inputFile)
{
    // compared to quickinfo / preview, this provides more info...

    PipelineManager m;
    Stage &r = m.makeReader(inputFile, "");
    FixedPointTable table(10000);
    r.prepare(table);
    return r.getMetadata();
}


void runPipelineParallel(point_count_t totalPoints, bool isStreaming, std::vector<std::unique_ptr<PipelineManager>>& pipelines, int max_threads)
{
    const int CHUNK_SIZE = 1'000'000;
    int num_chunks = totalPoints / CHUNK_SIZE;

    std::cout << "total points: " << (float)totalPoints / 1'000'000 << "M" << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    // https://github.com/gipert/progressbar
    progressbar bar(isStreaming ? num_chunks : pipelines.size());
    global_bar = &bar;

    std::cout << "jobs " << pipelines.size() << std::endl;
    std::cout << "max threads " << max_threads << std::endl;
    if (!isStreaming)
        std::cout << "running in non-streaming mode!" << std::endl;

    int nThreads = std::min( (int)pipelines.size(), max_threads );
    ThreadPool p(nThreads);
    for (size_t i = 0; i < pipelines.size(); ++i)
    {
        PipelineManager* pipeline = pipelines[i].get();
        if (isStreaming)
        {
            p.add([pipeline]() {

                MyTable table(CHUNK_SIZE);
                pipeline->executeStream(table);

            });
        }
        else
        {
            p.add([pipeline, &pipelines, i]() {
                pipeline->execute();
                pipelines[i].reset();  // to free the point table and views (meshes, rasters)
                bar_update();
                std::cout << "job " << i << " done" << std::endl;
            });
        }
    }

    //std::cout << "starting to wait" << std::endl;

    // while (p.tasksInQueue() + p.tasksInProgress())
    // {
    //     //std::cout << "progress: " << p.tasksInQueue() << " " << p.tasksInProgress() << " cnt " << cntPnt/1'000 << std::endl;
    //     std::this_thread::sleep_for(500ms);
    // }

    p.join();

    std::cerr << std::endl;

    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
    std::cout << "time " << duration.count()/1000. << " s" << std::endl;
}


static GDALDatasetH rasterTilesToVrt(const std::vector<std::string> &inputFiles, const std::string &outputVrtFile)
{
    // build a VRT so that all tiles can be handled as a single data source

    std::vector<const char*> dsNames;
    for ( const std::string &t : inputFiles )
    {
        dsNames.push_back(t.c_str());
    }

    // https://gdal.org/api/gdal_utils.html
    GDALDatasetH ds = GDALBuildVRT(outputVrtFile.c_str(), (int)dsNames.size(), nullptr, dsNames.data(), nullptr, nullptr);
    return ds;
}

static bool rasterVrtToCog(GDALDatasetH ds, const std::string &outputFile)
{
    const char* args[] = { "-of", "COG", "-co", "COMPRESS=DEFLATE", NULL };
    GDALTranslateOptions* psOptions = GDALTranslateOptionsNew((char**)args, NULL);

    GDALDatasetH dsFinal = GDALTranslate(outputFile.c_str(), ds, psOptions, nullptr);
    GDALTranslateOptionsFree(psOptions);
    if (!dsFinal)
        return false;
    GDALClose(dsFinal);
    return true;
}

bool rasterTilesToCog(const std::vector<std::string> &inputFiles, const std::string &outputFile)
{
    std::string outputVrt = outputFile;
    assert(ends_with(outputVrt, ".tif"));
    outputVrt.erase(outputVrt.rfind(".tif"), 4);
    outputVrt += ".vrt";

    GDALDatasetH ds = rasterTilesToVrt(inputFiles, outputVrt);

    if (!ds)
        return false;

    rasterVrtToCog(ds, outputFile);
    GDALClose(ds);

    // TODO: remove VRT + partial tifs after gdal_translate?

    return true;
}
