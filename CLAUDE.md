# CLAUDE.md

## 项目文档维护

每次修改项目代码后，必须更新相关文档：

1. **README.md** — 项目说明文档（结构、编译、部署）
2. **doc/learning.md** — 项目学习文档（技术细节、算法、API）
3. **doc/design.md** — 项目思路文档（设计决策、架构）
4. **doc/bugfix_mmz_ko.md** — 项目错误记录文档（Bug现象、根因、修复）

操作流程：
- 读取 `git diff` 了解变更内容
- 判断哪些文档需要更新
- 只修改受影响的部分，不要重写整个文档
- 如果文档不存在，创建并编写完整内容
- 更新完成后提交到 git

详细规范见 `.claude/skills/update-docs/SKILL.md`
