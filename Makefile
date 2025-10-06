STUID = 231180166
STUNAME = 杜明阳
TOKEN = 8eaNhZoH
# DO NOT modify the following code!!!

GITFLAGS = -q --author='tracer-ics2025 <tracer@njuics.org>' --no-verify --allow-empty

# prototype: git_commit(msg)
define git_commit
	-@git add $(NEMU_HOME)/.. -A --ignore-errors
	-@while (test -e .git/index.lock); do sleep 0.1; done
	-@(echo "> $(1)" && echo $(STUID) $(STUNAME) && uname -a && uptime) | git commit -F - $(GITFLAGS)
	-@sync
endef

_default:
	@echo "Please run 'make' under subprojects."

submit:
	git gc
	TOKEN=$(TOKEN) STUID=$(STUID) STUNAME=$(STUNAME) bash -c "$$(curl -s http://118.89.179.200:8080/static/submit.sh)"

.PHONY: default submit
