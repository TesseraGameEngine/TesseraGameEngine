#version 450

// Occlusion-query bounding boxes (VulkanPipeline::IssueOcclusionQueries): only the depth test
// matters, and the pipeline writes no colour. The query counts samples that pass.
void main() {
}
