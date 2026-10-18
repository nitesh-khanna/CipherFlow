
#include "JobQueue.hpp"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace vault::jobs {

void JobQueue::enqueue(const VaultJob& job) {
    jobs_.push(job);
}

void JobQueue::enqueueManifest(
    Action action,
    const manifest::VaultManifest& manifest
) {
    for (const manifest::FileManifest& file : manifest.files()) {
        for (const manifest::ChunkManifest& chunk : file.chunks) {
            enqueue(VaultJob{action, file, chunk});
        }
    }
}

bool JobQueue::executeAll(
    const crypto::EncryptionEngine& encryptionEngine,
    const crypto::CryptoKey& key,
    storage::StorageAdapter& storageAdapter,
    metadata::MetadataStore& metadataStore,
    const manifest::VaultManifest& manifest,
    std::string& error,
    std::size_t workerCount
) {
    if (!metadataStore.saveManifest(manifest, error)) {
        return false;
    }

    if (!storageAdapter.prepareWrite(manifest, error)) {
        return false;
    }

    // Preserve the original job order for writing results later.
    std::vector<VaultJob> orderedJobs;
    orderedJobs.reserve(jobs_.size());

    while (!jobs_.empty()) {
        orderedJobs.push_back(std::move(jobs_.front()));
        jobs_.pop();
    }

    struct JobResult {
        bool success = false;
        crypto::ByteBuffer output;
        std::string error;
    };

    std::vector<JobResult> results(orderedJobs.size());

    if (workerCount == 0) {
        workerCount = 1;
    }

    workerCount = std::min(workerCount, orderedJobs.size());

    // Workers consume job indices from this shared queue.
    std::queue<std::size_t> workQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondition;
    bool stopWorkers = false;

    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    try {
        for (std::size_t i = 0; i < workerCount; ++i) {
            workers.emplace_back([&]() {
                while (true) {
                    std::size_t jobIndex;

                    {
                        std::unique_lock<std::mutex> lock(queueMutex);

                        queueCondition.wait(lock, [&]() {
                            return stopWorkers || !workQueue.empty();
                        });

                        if (stopWorkers && workQueue.empty()) {
                            return;
                        }

                        jobIndex = workQueue.front();
                        workQueue.pop();
                    }

                    JobResult& result = results[jobIndex];

                    try {
                        result.success = processJob(
                            orderedJobs[jobIndex],
                            encryptionEngine,
                            key,
                            storageAdapter,
                            result.output,
                            result.error
                        );
                    } catch (const std::exception& exception) {
                        result.success = false;
                        result.error = exception.what();
                    } catch (...) {
                        result.success = false;
                        result.error = "unknown error while processing a job";
                    }
                }
            });
        }
    } catch (const std::exception& exception) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            stopWorkers = true;
        }

        queueCondition.notify_all();

        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        error = std::string("unable to start worker threads: ")
            + exception.what();

        storageAdapter.discardWrite(manifest);
        return false;
    }

    // Feed the workers. They can begin processing as soon as
    // each job index is added.
    for (std::size_t i = 0; i < orderedJobs.size(); ++i) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            workQueue.push(i);
        }

        queueCondition.notify_one();
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopWorkers = true;
    }

    queueCondition.notify_all();

    for (std::thread& worker : workers) {
        worker.join();
    }

    // Do not write any output until every job has succeeded.
    for (const JobResult& result : results) {
        if (!result.success) {
            error = result.error.empty()
                ? "job processing failed"
                : result.error;

            storageAdapter.discardWrite(manifest);
            return false;
        }
    }

    // Write results sequentially in the original order.
    // This prevents chunks from being appended out of order.
    for (std::size_t i = 0; i < orderedJobs.size(); ++i) {
        if (!storageAdapter.writeChunk(
                orderedJobs[i].file,
                orderedJobs[i].chunk,
                results[i].output,
                error
            )) {
            storageAdapter.discardWrite(manifest);
            return false;
        }
    }

    if (!storageAdapter.commitWrite(manifest, error)) {
        storageAdapter.discardWrite(manifest);
        return false;
    }

    return true;
}

bool JobQueue::empty() const {
    return jobs_.empty();
}

std::size_t JobQueue::size() const {
    return jobs_.size();
}

bool JobQueue::processJob(
    const VaultJob& job,
    const crypto::EncryptionEngine& encryptionEngine,
    const crypto::CryptoKey& key,
    const storage::StorageAdapter& storageAdapter,
    crypto::ByteBuffer& output,
    std::string& error
) const {
    crypto::ByteBuffer input;

    if (!storageAdapter.readChunk(job.file, job.chunk, input, error)) {
        return false;
    }

    crypto::EncryptionContext context;
    context.fileId = job.file.fileId;
    context.chunkIndex = job.chunk.index;
    context.offset = job.chunk.offset;
    context.plainSize = job.chunk.plainSize;
    context.storageKey = job.chunk.storageKey;

    return job.action == Action::Encrypt
        ? encryptionEngine.encrypt(input, key, context, output, error)
        : encryptionEngine.decrypt(input, key, context, output, error);
}

} // namespace vault::jobs
