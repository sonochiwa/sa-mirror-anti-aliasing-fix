#pragma once

// Redirects the mirror camera pass in CMirrors::BeforeMainRender into a
// render target `factor` times the mirror size and reduces it back into the
// mirror texture. Returns false when the executable does not match or the
// calls could not be rewritten.
bool InstallSupersampling(int factor);
