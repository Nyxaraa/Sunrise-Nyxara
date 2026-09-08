-- Native music sections from 80B3C904: one choice group, 29 ordered sections.
-- The bank owns musical transitions; scripts only select a section at milestones.
-- Section 5's authored name resolves to tumbler_exit. Later section-to-encounter
-- pacing is reconstructed from package order and needs an audible full-route test.
return function(m)
    local M = {}
    function M.update(c, s)
        local section
        local region = s:variable("ember.region")
        if s:variable("ember.ending") then section = -1
        elseif region == 0 then
            local p = s:variable("ember.apex.phase") or 0
            section = ({[0]=19,[1]=19,[2]=20,[3]=22,[4]=25,[5]=27,[6]=28,[7]=-1})[p]
            if p == 2 and s:variable("ember.r.electron_controllers.phase") == 2 then section = 21 end
            if p == 3 and s:variable("ember.r.cue.41") then section = 23 end
            if p == 3 and (s:variable("ember.apex.dead.EAST") or s:variable("ember.apex.dead.WEST")) then section = 24 end
            if p == 4 and s:variable("ember.r.cue.46") then section = 26 end
        elseif region == 40 then
            local p = s:variable("ember.cinder.phase") or 0
            section = ({[0]=5,[1]=6,[2]=6,[3]=7,[4]=7,[5]=8,[6]=9,[7]=10,[8]=11,
                [9]=12,[10]=13,[11]=15,[12]=16,[13]=16,[14]=17})[p]
        elseif region == 56 then
            section = ({[0]=3,[1]=3,[2]=3,[3]=4,[4]=5})[s:variable("ember.processing.phase") or 0]
        elseif region == 64 then
            section = s:variable("ember.bridge.extended") and 2
                or ((s:variable("ember.route.progress") or 0) >= 5 and 1 or 0)
        end
        if section == nil or s:variable("ember.music.section") == section then return end
        c:slot(assert(m.Slot.M_MUSIC_SENSOR_80B3C90A)):set_music_section{
            section = math.max(section, 0), enabled = section >= 0}
        c:set_variable("ember.music.section", section)
    end
    return M
end
