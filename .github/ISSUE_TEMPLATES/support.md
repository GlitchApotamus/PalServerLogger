---
name: Support Request / Question
about: Ask a question or get help setting up the logger
title: '[SUPPORT] '
labels: 'question'
assignees: ''
---

**Which server setup are you running?**
- [ ] Vanilla Palworld Server
- [ ] Server with PalDefender
- [ ] Other mods installed (please list below)

**Describe your question or issue:**
A clear and concise description of what you need help with.

**Configuration File (`d3d9_config.json`):**
```json
// Paste your config here (remove sensitive info if any)
```
---

### 2. Pull Request Template (`pull_request_template.md`)
Unlike issue templates which live in `ISSUE_TEMPLATE`, this one goes directly into the root `.github/` folder (`.github/pull_request_template.md`). It forces contributors to outline what their pull request changes before they submit it.

```markdown
## Description
Please include a summary of the change and which issue is fixed. Include relevant motivation and context.

## Type of Change
- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation update

## How Has This Been Tested?
Please describe the tests that you ran to verify your changes. 
- [ ] Tested locally on Windows server build
- [ ] Verified JSON config loading & translations

## Checklist:
- [ ] My code follows the style guidelines of this project
- [ ] I have performed a self-review of my code
- [ ] I have updated the documentation where necessary